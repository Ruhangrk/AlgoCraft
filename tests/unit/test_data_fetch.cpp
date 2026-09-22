#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/session_calendar.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_fetch_service.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/persistence/coverage_repository.hpp"
#include "algocraft/persistence/rocks_bar_store.hpp"
#include "algocraft/persistence/sqlite_database.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

#ifndef ALGOCRAFT_DATA_DIR
#define ALGOCRAFT_DATA_DIR "data/1min"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_fetch" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

algocraft::Timestamp ist_ns(int y, int mon, int d, int hour, int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{y} / mon / d} + hours{hour} + minutes{minute} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

void write_two_sessions(const std::filesystem::path& path) {
  std::ofstream out(path);
  out << "timestamp_ns,open,high,low,close,volume\n";
  const auto a = ist_ns(2026, 8, 28, 9, 15);
  const auto b = ist_ns(2026, 8, 28, 9, 16);
  const auto c = ist_ns(2026, 8, 31, 9, 15);
  const auto d = ist_ns(2026, 8, 31, 9, 16);
  out << a.nanos() << ",100.00,101.00,99.00,100.50,10\n";
  out << b.nanos() << ",100.50,102.00,100.00,101.00,11\n";
  out << c.nanos() << ",101.00,103.00,100.00,102.00,12\n";
  out << d.nanos() << ",102.00,104.00,101.00,103.00,13\n";
}

struct CacheStack {
  algocraft::SqliteDatabase db;
  algocraft::RocksBarStore bars;
  std::optional<algocraft::CoverageRepository> coverage;

  explicit CacheStack(const std::filesystem::path& root)
      : db([&] {
          algocraft::PersistenceConfig cfg;
          cfg.db_path = root / "algocraft.db";
          cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
          return cfg;
        }()),
        bars(root / "bars") {
    db.open();
    db.migrate();
    bars.open();
    coverage.emplace(db.handle());
  }

  algocraft::CoverageRepository& cov() { return *coverage; }
};

algocraft::BarEvent bar_at(algocraft::SymbolId id, int y, int mon, int d, int minute,
                           std::int64_t close_paise) {
  algocraft::BarEvent bar{};
  bar.symbol_id = id;
  bar.timestamp = ist_ns(y, mon, d, 9, 15 + minute);
  bar.resolution = algocraft::BarResolution::OneMin;
  bar.open = algocraft::Price::from_paise(close_paise);
  bar.high = algocraft::Price::from_paise(close_paise);
  bar.low = algocraft::Price::from_paise(close_paise);
  bar.close = algocraft::Price::from_paise(close_paise);
  bar.volume = algocraft::Quantity::from_shares(1);
  return bar;
}

class ScriptedLoader final : public algocraft::HistoricalDataLoader {
public:
  std::map<algocraft::SessionDate, std::vector<algocraft::BarEvent>> days{};
  int calls{0};

  std::vector<algocraft::BarEvent> load_bars(algocraft::SymbolId, algocraft::Timestamp from,
                                             algocraft::Timestamp to,
                                             algocraft::BarResolution resolution) override {
    ++calls;
    std::vector<algocraft::BarEvent> out;
    for (const auto& [_, bars] : days) {
      for (const auto& bar : bars) {
        if (from.nanos() != 0 && bar.timestamp < from) {
          continue;
        }
        if (to.nanos() != 0 && bar.timestamp > to) {
          continue;
        }
        auto copy = bar;
        copy.resolution = resolution;
        out.push_back(copy);
      }
    }
    return out;
  }
};

void expect_contiguous_keys(algocraft::BarStore& store, const char* ticker,
                            algocraft::SessionDate first, algocraft::SessionDate last) {
  for (const auto d : algocraft::session_days(first, last)) {
    EXPECT_TRUE(store.has_session(ticker, algocraft::BarResolution::OneMin, d)) << d.iso();
  }
}

}  // namespace

TEST(DataFetchService, SecondLoadDoesNotRereadCsv) {
  const auto dir = make_temp_dir();
  write_two_sessions(dir / "AAA.csv");
  CacheStack stack(dir / "persist");

  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});

  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(dir, &symbols), stack.bars, stack.cov(), symbols));

  auto& cached = dynamic_cast<algocraft::CachedProvider&>(registry.active_provider());
  cached.fetch().set_now(ist_ns(2026, 8, 31, 18, 0));
  const auto first = cached.historical_loader().load_bars(id, {}, {}, algocraft::BarResolution::OneMin);
  ASSERT_EQ(first.size(), 4u);
  EXPECT_EQ(cached.fetch().vendor_fetches(), 2u);

  const auto second =
      cached.historical_loader().load_bars(id, {}, {}, algocraft::BarResolution::OneMin);
  EXPECT_EQ(second.size(), first.size());
  EXPECT_EQ(second.back().close.paise(), first.back().close.paise());
  EXPECT_EQ(cached.fetch().vendor_fetches(), 2u);

  auto& csv = dynamic_cast<algocraft::CsvProvider&>(cached.vendor());
  EXPECT_EQ(csv.csv_loader().load_calls(), 2u);

  const auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->first_date->iso(), "2026-08-28");
  EXPECT_EQ(row->last_date->iso(), "2026-08-28");
  EXPECT_EQ(row->live_date->iso(), "2026-08-31");
  EXPECT_EQ(row->sessions, 2);
  EXPECT_EQ(row->source, "csv");

  EXPECT_TRUE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                     algocraft::SessionDate::from_iso("2026-08-28")));
  EXPECT_FALSE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                      algocraft::SessionDate::from_iso("2026-08-29")));

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, NseCsvIngestThenCache) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);

  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "RELIANCE"}, {});
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_DATA_DIR, &symbols), stack.bars,
      stack.cov(), symbols));

  auto& cached = dynamic_cast<algocraft::CachedProvider&>(registry.active_provider());
  cached.fetch().set_now(ist_ns(2026, 9, 11, 18, 0));
  const auto a =
      cached.historical_loader().load_bars(id, {}, {}, algocraft::BarResolution::OneMin);
  const auto fetches = cached.fetch().vendor_fetches();
  const auto b =
      cached.historical_loader().load_bars(id, {}, {}, algocraft::BarResolution::OneMin);
  EXPECT_GE(a.size(), 1000u);
  EXPECT_EQ(a.size(), b.size());
  EXPECT_EQ(a.front().timestamp.nanos(), b.front().timestamp.nanos());
  EXPECT_EQ(a.back().close.paise(), b.back().close.paise());
  EXPECT_EQ(cached.fetch().vendor_fetches(), fetches);
  EXPECT_EQ(fetches, 2u);

  const auto row = stack.cov().get("RELIANCE", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->first_date->iso(), "2026-08-18");
  EXPECT_EQ(row->last_date->iso(), "2026-09-10");
  EXPECT_EQ(row->live_date->iso(), "2026-09-11");
  EXPECT_GT(row->sessions, 10);

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, RequestInsideRangeNoVendorCall) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});
  ScriptedLoader loader;
  const auto d28 = algocraft::SessionDate::from_iso("2026-08-28");
  const auto d31 = algocraft::SessionDate::from_iso("2026-08-31");
  const auto d01 = algocraft::SessionDate::from_iso("2026-09-01");
  loader.days[d28] = {bar_at(id, 2026, 8, 28, 0, 10000)};
  loader.days[d31] = {bar_at(id, 2026, 8, 31, 0, 10100)};
  loader.days[d01] = {bar_at(id, 2026, 9, 1, 0, 10200)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols);
  fetch.set_now(ist_ns(2026, 9, 2, 12, 0));
  fetch.load_bars(id, ist_ns(2026, 8, 28, 0, 0), ist_ns(2026, 9, 1, 23, 59),
                  algocraft::BarResolution::OneMin);
  const auto n = fetch.vendor_fetches();
  EXPECT_GE(n, 1u);
  fetch.load_bars(id, ist_ns(2026, 8, 31, 0, 0), ist_ns(2026, 9, 1, 23, 59),
                  algocraft::BarResolution::OneMin);
  EXPECT_EQ(fetch.vendor_fetches(), n);
  const auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->first_date->iso(), "2026-08-28");
  EXPECT_EQ(row->last_date->iso(), "2026-09-01");
  expect_contiguous_keys(stack.bars, "AAA", d28, d01);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, ExtendsRightAndLeftContiguous) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});
  ScriptedLoader loader;
  loader.days[algocraft::SessionDate::from_iso("2026-08-28")] = {bar_at(id, 2026, 8, 28, 0, 10000)};
  loader.days[algocraft::SessionDate::from_iso("2026-08-31")] = {bar_at(id, 2026, 8, 31, 0, 10100)};
  loader.days[algocraft::SessionDate::from_iso("2026-09-01")] = {bar_at(id, 2026, 9, 1, 0, 10200)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols);
  fetch.set_now(ist_ns(2026, 9, 2, 12, 0));
  fetch.load_bars(id, ist_ns(2026, 8, 31, 0, 0), ist_ns(2026, 8, 31, 23, 59),
                  algocraft::BarResolution::OneMin);
  auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->first_date->iso(), "2026-08-31");
  EXPECT_EQ(row->last_date->iso(), "2026-08-31");

  fetch.load_bars(id, ist_ns(2026, 8, 28, 0, 0), ist_ns(2026, 9, 1, 23, 59),
                  algocraft::BarResolution::OneMin);
  row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->first_date->iso(), "2026-08-28");
  EXPECT_EQ(row->last_date->iso(), "2026-09-01");
  expect_contiguous_keys(stack.bars, "AAA", *row->first_date, *row->last_date);
  EXPECT_FALSE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                      algocraft::SessionDate::from_iso("2026-08-29")));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, TodayStaleRefetchOnly) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});
  ScriptedLoader loader;
  loader.days[algocraft::SessionDate::from_iso("2026-09-01")] = {bar_at(id, 2026, 9, 1, 0, 10000)};
  loader.days[algocraft::SessionDate::from_iso("2026-09-02")] = {bar_at(id, 2026, 9, 2, 0, 10100)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols);
  fetch.set_now(ist_ns(2026, 9, 2, 12, 0));
  fetch.load_bars(id, ist_ns(2026, 9, 1, 0, 0), ist_ns(2026, 9, 2, 12, 0),
                  algocraft::BarResolution::OneMin);
  const auto n = fetch.vendor_fetches();
  auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->last_date->iso(), "2026-09-01");
  EXPECT_EQ(row->live_date->iso(), "2026-09-02");

  fetch.set_now(ist_ns(2026, 9, 2, 12, 10));
  fetch.load_bars(id, ist_ns(2026, 9, 1, 0, 0), ist_ns(2026, 9, 2, 12, 10),
                  algocraft::BarResolution::OneMin);
  EXPECT_EQ(fetch.vendor_fetches(), n);

  fetch.set_now(ist_ns(2026, 9, 2, 12, 41));
  fetch.load_bars(id, ist_ns(2026, 9, 1, 0, 0), ist_ns(2026, 9, 2, 12, 41),
                  algocraft::BarResolution::OneMin);
  EXPECT_EQ(fetch.vendor_fetches(), n + 1);
  row = stack.cov().get("AAA", "1m");
  EXPECT_EQ(row->last_date->iso(), "2026-09-01");
  EXPECT_EQ(row->live_date->iso(), "2026-09-02");
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, MidnightSealsPreviousLiveAndSkipsWeekend) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});
  ScriptedLoader loader;
  loader.days[algocraft::SessionDate::from_iso("2026-09-03")] = {bar_at(id, 2026, 9, 3, 0, 10000)};
  loader.days[algocraft::SessionDate::from_iso("2026-09-04")] = {bar_at(id, 2026, 9, 4, 0, 10100)};
  loader.days[algocraft::SessionDate::from_iso("2026-09-07")] = {bar_at(id, 2026, 9, 7, 0, 10200)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols);
  fetch.set_now(ist_ns(2026, 9, 4, 16, 0));
  fetch.load_bars(id, ist_ns(2026, 9, 3, 0, 0), ist_ns(2026, 9, 4, 16, 0),
                  algocraft::BarResolution::OneMin);
  auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->last_date->iso(), "2026-09-03");
  EXPECT_EQ(row->live_date->iso(), "2026-09-04");

  fetch.set_now(ist_ns(2026, 9, 7, 10, 0));
  fetch.load_bars(id, ist_ns(2026, 9, 3, 0, 0), ist_ns(2026, 9, 7, 10, 0),
                  algocraft::BarResolution::OneMin);
  row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->last_date->iso(), "2026-09-04");
  EXPECT_EQ(row->live_date->iso(), "2026-09-07");
  EXPECT_FALSE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                      algocraft::SessionDate::from_iso("2026-09-05")));
  EXPECT_FALSE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                      algocraft::SessionDate::from_iso("2026-09-06")));
  expect_contiguous_keys(stack.bars, "AAA", *row->first_date, *row->last_date);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, EmptyClosedDaySkippedAndLeftExtendStops) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});
  ScriptedLoader loader;
  loader.days[algocraft::SessionDate::from_iso("2026-08-31")] = {bar_at(id, 2026, 8, 31, 0, 10100)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols);
  fetch.set_now(ist_ns(2026, 9, 2, 12, 0));
  fetch.load_bars(id, ist_ns(2026, 8, 31, 0, 0), ist_ns(2026, 9, 1, 23, 59),
                  algocraft::BarResolution::OneMin);
  EXPECT_FALSE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                      algocraft::SessionDate::from_iso("2026-09-01")));

  fetch.load_bars(id, ist_ns(2026, 8, 24, 0, 0), ist_ns(2026, 8, 31, 23, 59),
                  algocraft::BarResolution::OneMin);
  const auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->first_date->iso(), "2026-08-31");
  EXPECT_FALSE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                      algocraft::SessionDate::from_iso("2026-08-28")));
  expect_contiguous_keys(stack.bars, "AAA", *row->first_date, *row->last_date);
  EXPECT_NE(row->last_date->iso(), "2026-09-02");
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, TodayOnlyNeverEntersClosedRange) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "AAA"}, {});
  ScriptedLoader loader;
  loader.days[algocraft::SessionDate::from_iso("2026-09-02")] = {bar_at(id, 2026, 9, 2, 0, 10100)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols);
  fetch.set_now(ist_ns(2026, 9, 2, 12, 0));
  fetch.load_bars(id, ist_ns(2026, 9, 2, 0, 0), ist_ns(2026, 9, 2, 12, 0),
                  algocraft::BarResolution::OneMin);

  const auto row = stack.cov().get("AAA", "1m");
  ASSERT_TRUE(row);
  EXPECT_FALSE(row->first_date.has_value());
  EXPECT_FALSE(row->last_date.has_value());
  ASSERT_TRUE(row->live_date);
  EXPECT_EQ(row->live_date->iso(), "2026-09-02");
  EXPECT_TRUE(stack.bars.has_session("AAA", algocraft::BarResolution::OneMin,
                                     algocraft::SessionDate::from_iso("2026-09-02")));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(DataFetchService, ChartDailyEnsureSecondCallSkipsVendor) {
  const auto dir = make_temp_dir();
  CacheStack stack(dir);

  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "INFY"}, {});
  ScriptedLoader loader;
  // Daily bars in 2025 and 2026 (timestamps at IST midnight-ish via 9:15 helper is fine).
  loader.days[algocraft::SessionDate::from_iso("2025-06-15")] = {
      bar_at(id, 2025, 6, 15, 0, 150000)};
  loader.days[algocraft::SessionDate::from_iso("2026-01-10")] = {
      bar_at(id, 2026, 1, 10, 0, 160000)};
  loader.days[algocraft::SessionDate::from_iso("2026-03-01")] = {
      bar_at(id, 2026, 3, 1, 0, 161000)};

  algocraft::DataFetchService fetch(stack.bars, stack.cov(), loader, symbols, "scripted");
  fetch.set_now(ist_ns(2026, 3, 15, 12, 0));

  const auto from = ist_ns(2025, 6, 1, 0, 0);
  const auto to = ist_ns(2026, 3, 10, 23, 59);
  fetch.ensure_data_available("INFY", from, to, algocraft::BarResolution::OneDay);
  const auto first_fetches = fetch.vendor_fetches();
  EXPECT_EQ(first_fetches, 2u);  // years 2025 and 2026
  EXPECT_EQ(loader.calls, 2);
  EXPECT_TRUE(stack.bars.has_session("INFY", algocraft::BarResolution::OneDay,
                                     algocraft::SessionDate::from_iso("2025-01-01")));
  EXPECT_TRUE(stack.bars.has_session("INFY", algocraft::BarResolution::OneDay,
                                     algocraft::SessionDate::from_iso("2026-12-01")));

  const auto cov = stack.cov().get("INFY", "1d");
  ASSERT_TRUE(cov);
  EXPECT_EQ(cov->first_date->year(), 2025);
  EXPECT_EQ(cov->last_date->year(), 2026);

  fetch.ensure_data_available("INFY", from, to, algocraft::BarResolution::OneDay);
  EXPECT_EQ(fetch.vendor_fetches(), first_fetches);
  EXPECT_EQ(loader.calls, 2);

  const auto bars =
      fetch.load_bars(id, from, to, algocraft::BarResolution::OneDay);
  ASSERT_EQ(bars.size(), 3u);
  EXPECT_EQ(bars.front().resolution, algocraft::BarResolution::OneDay);

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
