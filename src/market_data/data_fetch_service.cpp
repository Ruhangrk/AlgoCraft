#include "algocraft/market_data/data_fetch_service.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "algocraft/domain/session_calendar.hpp"

namespace algocraft {
namespace {

Timestamp ist_hm(SessionDate date, int hour, int minute) {
  using namespace std::chrono;
  const auto utc = sys_days{year{date.year()} / date.month() / date.day()} + hours{hour} +
                   minutes{minute} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

Timestamp day_start(SessionDate date) { return ist_hm(date, 0, 0); }
Timestamp day_end(SessionDate date) { return ist_hm(date, 23, 59); }

int calendar_span_days(SessionDate from, SessionDate to) {
  int n = 1;
  for (auto d = from; d < to; d = next_calendar_day(d)) {
    ++n;
  }
  return n;
}

std::vector<std::vector<SessionDate>> chunk_days(const std::vector<SessionDate>& days) {
  std::vector<std::vector<SessionDate>> chunks;
  std::vector<SessionDate> cur;
  for (const auto d : days) {
    if (cur.empty()) {
      cur.push_back(d);
      continue;
    }
    if (calendar_span_days(cur.front(), d) > DataFetchService::kMaxChunkCalendarDays) {
      chunks.push_back(cur);
      cur = {d};
    } else {
      cur.push_back(d);
    }
  }
  if (!cur.empty()) {
    chunks.push_back(std::move(cur));
  }
  return chunks;
}

}  // namespace

DataFetchService::DataFetchService(BarStore& store, CoverageRepository& coverage,
                                   HistoricalDataLoader& vendor, const SymbolTable& symbols,
                                   std::string source)
    : store_(&store),
      coverage_(&coverage),
      vendor_(&vendor),
      symbols_(&symbols),
      source_(std::move(source)) {}

Timestamp DataFetchService::now() const {
  return now_override_.has_value() ? *now_override_ : Timestamp::now();
}

SymbolId DataFetchService::require_symbol(std::string_view ticker) const {
  const auto found = symbols_->find(std::string{ticker});
  if (!found) {
    throw std::out_of_range("unknown ticker: " + std::string{ticker});
  }
  return *found;
}

bool DataFetchService::live_fresh(const CoverageRow& row) const {
  if (!row.last_fetched_at) {
    return false;
  }
  try {
    const auto fetched = std::stoll(*row.last_fetched_at);
    return now().nanos() - fetched <= kLiveTtlNs;
  } catch (...) {
    return false;
  }
}

void DataFetchService::persist_coverage(std::string_view ticker, BarResolution resolution,
                                        CoverageRow row) {
  const auto code = std::string{bar_resolution_code(resolution)};
  row.ticker = std::string{ticker};
  row.resolution = code;
  row.source = source_;
  const auto listed = store_->list_sessions(row.ticker, resolution, {}, {});
  row.sessions = static_cast<std::int32_t>(listed.size());
  coverage_->upsert(row);
}

void DataFetchService::ensure_data_available(std::string_view ticker, Timestamp from, Timestamp to,
                                             BarResolution resolution) {
  if (ticker.empty()) {
    throw std::invalid_argument("ensure_data_available requires ticker");
  }

  const auto now_ts = now();
  if (to.nanos() == 0 || to > now_ts) {
    to = now_ts;
  }

  // Chart TFs (1d/1w/1M): year blobs, no session-day / live-today logic.
  if (is_chart_resolution(resolution)) {
    ensure_chart_available(ticker, from, to, resolution);
    return;
  }

  const auto today = SessionDate::from_ist(now_ts);
  const bool today_is_session = is_nse_session_day(today);
  const auto closed_end = last_closed_session(today);

  seal_stale_live(ticker, resolution, today, today_is_session);

  auto row = coverage_->get(ticker, std::string{bar_resolution_code(resolution)});
  const auto to_d = SessionDate::from_ist(to);
  auto closed_to = closed_end;
  if (to_d.ok() && to_d < closed_to) {
    closed_to = to_d;
  }

  if (from.nanos() == 0) {
    if (row && row->first_date) {
      fill_closed(ticker, *row->first_date, closed_to, resolution);
    } else {
      discover_closed(ticker, closed_to, resolution);
    }
  } else {
    const auto from_d = SessionDate::from_ist(from);
    if (from_d.ok() && from_d <= closed_to) {
      fill_closed(ticker, from_d, closed_to, resolution);
    }
  }

  const bool want_today =
      today_is_session && to_d >= today && (from.nanos() == 0 || SessionDate::from_ist(from) <= today);
  if (want_today) {
    refresh_today(ticker, today, resolution);
  }
}

void DataFetchService::ensure_chart_available(std::string_view ticker, Timestamp from, Timestamp to,
                                              BarResolution resolution) {
  const auto now_ts = now();
  if (to.nanos() == 0 || to > now_ts) {
    to = now_ts;
  }
  const auto to_d = SessionDate::from_ist(to);
  if (!to_d.ok()) {
    return;
  }

  int from_year = to_d.year();
  if (from.nanos() != 0) {
    const auto from_d = SessionDate::from_ist(from);
    if (from_d.ok()) {
      from_year = from_d.year();
    }
  } else {
    // Open-ended from: only backfill years already in coverage, else current year.
    const auto row = coverage_->get(ticker, std::string{bar_resolution_code(resolution)});
    if (row && row->first_date) {
      from_year = row->first_date->year();
    }
  }
  if (from_year > to_d.year()) {
    return;
  }

  const auto ticker_s = std::string{ticker};
  const auto symbol_id = require_symbol(ticker_s);
  const auto code = std::string{bar_resolution_code(resolution)};

  for (int year = from_year; year <= to_d.year(); ++year) {
    const auto period = SessionDate::from_parts(year, 1, 1);
    if (store_->has_session(ticker_s, resolution, period)) {
      continue;
    }

    const auto year_from = day_start(SessionDate::from_parts(year, 1, 1));
    auto year_to = day_end(SessionDate::from_parts(year, 12, 31));
    if (year_to > now_ts) {
      year_to = now_ts;
    }
    if (year_from > year_to) {
      continue;
    }

    ++vendor_fetches_;
    auto bars = vendor_->load_bars(symbol_id, year_from, year_to, resolution);
    // Always write the year key (possibly empty) so a second ensure does not re-hit vendor.
    store_->put_session(ticker_s, resolution, period, bars);

    auto row = coverage_->get(ticker_s, code).value_or(CoverageRow{});
    if (!row.first_date || period < *row.first_date) {
      row.first_date = period;
    }
    if (!row.last_date || period > *row.last_date) {
      row.last_date = period;
    }
    persist_coverage(ticker_s, resolution, row);
  }
}

std::vector<BarEvent> DataFetchService::load_bars(SymbolId symbol_id, Timestamp from, Timestamp to,
                                                  BarResolution resolution) {
  if (!symbols_->contains(symbol_id)) {
    throw std::out_of_range("unknown symbol id");
  }
  const auto ticker = std::string{symbols_->symbol(symbol_id).ticker};
  ensure_data_available(ticker, from, to, resolution);

  const auto from_d = from.nanos() == 0 ? SessionDate{} : SessionDate::from_ist(from);
  auto to_use = to;
  if (to_use.nanos() == 0 || to_use > now()) {
    to_use = now();
  }
  const auto to_d = SessionDate::from_ist(to_use);
  const auto dates = store_->list_sessions(ticker, resolution, from_d, to_d);

  std::vector<BarEvent> out;
  for (const auto date : dates) {
    auto blob = store_->get_session(ticker, resolution, date, symbol_id);
    if (!blob) {
      continue;
    }
    for (auto& bar : *blob) {
      if (from.nanos() != 0 && bar.timestamp < from) {
        continue;
      }
      if (to.nanos() != 0 && bar.timestamp > to) {
        continue;
      }
      out.push_back(bar);
    }
  }
  return out;
}

void DataFetchService::seal_stale_live(std::string_view ticker, BarResolution resolution,
                                       SessionDate today, bool today_is_session) {
  const auto code = std::string{bar_resolution_code(resolution)};
  auto row = coverage_->get(ticker, code);
  if (!row || !row->live_date) {
    return;
  }
  if (today_is_session && *row->live_date == today) {
    return;
  }
  fetch_and_store(ticker, {*row->live_date}, resolution, false);
  row = coverage_->get(ticker, code).value_or(CoverageRow{});
  if (!row->last_date || *row->live_date > *row->last_date) {
    row->last_date = row->live_date;
  }
  if (!row->first_date) {
    row->first_date = row->live_date;
  }
  row->live_date.reset();
  persist_coverage(ticker, resolution, *row);
}

void DataFetchService::fill_closed(std::string_view ticker, SessionDate from, SessionDate to,
                                   BarResolution resolution) {
  const auto code = std::string{bar_resolution_code(resolution)};
  auto row = coverage_->get(ticker, code);
  const auto requested = session_days(from, to);
  if (requested.empty()) {
    return;
  }

  std::set<SessionDate> covered;
  // Only trust days with a non-empty Rocks blob. Empty keys (failed/partial
  // vendor writes) used to block refetch forever → OHLCV candles=[].
  if (row && row->first_date && row->last_date) {
    for (const auto d : session_days(*row->first_date, *row->last_date)) {
      auto blob = store_->get_session(std::string{ticker}, resolution, d, /*symbol_id=*/0);
      if (blob && !blob->empty()) {
        covered.insert(d);
      }
    }
  }

  std::vector<SessionDate> left;
  std::vector<SessionDate> rest;
  for (const auto d : requested) {
    if (covered.contains(d)) {
      continue;
    }
    if (row && row->first_date && d < *row->first_date) {
      left.push_back(d);
    } else {
      rest.push_back(d);
    }
  }

  if (!left.empty()) {
    fetch_and_store(ticker, left, resolution, true);
  }
  if (!rest.empty()) {
    fetch_and_store(ticker, rest, resolution, row && row->first_date ? false : true);
  }
}

void DataFetchService::discover_closed(std::string_view ticker, SessionDate closed_to,
                                       BarResolution resolution) {
  if (!closed_to.ok()) {
    return;
  }
  const auto ticker_s = std::string{ticker};
  const auto symbol_id = require_symbol(ticker_s);
  ++vendor_fetches_;
  const auto bars = vendor_->load_bars(symbol_id, {}, day_end(closed_to), resolution);
  std::map<SessionDate, std::vector<BarEvent>> by_day;
  for (auto& bar : bars) {
    const auto d = SessionDate::from_ist(bar.timestamp);
    if (d > closed_to) {
      continue;
    }
    by_day[d].push_back(std::move(bar));
  }
  if (by_day.empty()) {
    return;
  }
  const auto first = by_day.begin()->first;
  for (const auto d : session_days(first, closed_to)) {
    store_->put_session(ticker_s, resolution, d, by_day[d]);
  }
  CoverageRow row{};
  row.first_date = first;
  row.last_date = closed_to;
  persist_coverage(ticker_s, resolution, row);
}

void DataFetchService::refresh_today(std::string_view ticker, SessionDate today,
                                     BarResolution resolution) {
  const auto code = std::string{bar_resolution_code(resolution)};
  auto row = coverage_->get(ticker, code).value_or(CoverageRow{});
  if (row.live_date && *row.live_date == today && live_fresh(row)) {
    const auto blob = store_->get_session(std::string{ticker}, resolution, today, /*symbol_id=*/0);
    if (blob && !blob->empty()) {
      return;
    }
    // Empty live blob: fall through and refetch (do not trust TTL alone).
  }
  fetch_and_store(ticker, {today}, resolution, false);
  row = coverage_->get(ticker, code).value_or(CoverageRow{});
  // Closed range (first/last) never includes today — only live_date may.
  const auto closed_end = last_closed_session(today);
  if (row.last_date && *row.last_date > closed_end) {
    row.last_date = closed_end;
  }
  if (row.first_date && row.last_date && *row.first_date > *row.last_date) {
    row.first_date = row.last_date;
  }
  const auto blob = store_->get_session(std::string{ticker}, resolution, today, /*symbol_id=*/0);
  if (blob && !blob->empty()) {
    row.live_date = today;
    row.last_fetched_at = std::to_string(now().nanos());
  } else {
    // Keep retrying later; do not publish an empty live day as fresh.
    row.live_date.reset();
    row.last_fetched_at.reset();
  }
  persist_coverage(ticker, resolution, row);
}

void DataFetchService::fetch_and_store(std::string_view ticker, const std::vector<SessionDate>& days,
                                       BarResolution resolution, bool left_extend) {
  if (days.empty()) {
    return;
  }
  const auto ticker_s = std::string{ticker};
  const auto symbol_id = require_symbol(ticker_s);
  const auto code = std::string{bar_resolution_code(resolution)};
  // first/last are closed sessions only — never "today" (live_date owns that).
  const auto closed_end = last_closed_session(SessionDate::from_ist(now()));

  for (const auto& chunk : chunk_days(days)) {
    ++vendor_fetches_;
    const auto bars =
        vendor_->load_bars(symbol_id, day_start(chunk.front()), day_end(chunk.back()), resolution);
    std::map<SessionDate, std::vector<BarEvent>> by_day;
    for (auto& bar : bars) {
      by_day[SessionDate::from_ist(bar.timestamp)].push_back(std::move(bar));
    }

    std::vector<SessionDate> to_write = chunk;
    if (left_extend) {
      std::optional<SessionDate> first_with_bars;
      for (const auto d : chunk) {
        if (by_day.contains(d) && !by_day[d].empty()) {
          first_with_bars = d;
          break;
        }
      }
      if (!first_with_bars) {
        continue;
      }
      to_write.clear();
      for (const auto d : chunk) {
        if (d >= *first_with_bars) {
          to_write.push_back(d);
        }
      }
    }

    for (const auto d : to_write) {
      const auto& day_bars = by_day[d];
      // Do not persist empty closed-day blobs — they poison coverage/has_session.
      // Live today may still be written empty-skipped here; refresh_today owns TTL.
      if (day_bars.empty()) {
        continue;
      }
      store_->put_session(ticker_s, resolution, d, day_bars);
    }

    auto row = coverage_->get(ticker_s, code).value_or(CoverageRow{});
    bool touched = false;
    for (const auto d : to_write) {
      if (!by_day.contains(d) || by_day[d].empty()) {
        continue;
      }
      // Rocks may hold today's live blob; closed coverage range stops at closed_end.
      if (d > closed_end) {
        continue;
      }
      if (!row.first_date || d < *row.first_date) {
        row.first_date = d;
        touched = true;
      }
      if (!row.last_date || d > *row.last_date) {
        row.last_date = d;
        touched = true;
      }
    }
    if (touched && row.first_date) {
      persist_coverage(ticker_s, resolution, row);
    }
  }
}

}  // namespace algocraft
