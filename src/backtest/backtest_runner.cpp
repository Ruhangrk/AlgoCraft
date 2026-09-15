#include "algocraft/backtest/backtest_runner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft {
namespace {

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;
constexpr std::int64_t kNanosPerSecond = 1'000'000'000LL;

static_assert(std::is_trivially_copyable_v<BarEvent>);

double sharpe_from_equity(const std::vector<std::int64_t>& equity) {
  if (equity.size() < 3) {
    return 0;
  }
  std::vector<double> rets;
  rets.reserve(equity.size() - 1);
  for (std::size_t i = 1; i < equity.size(); ++i) {
    if (equity[i - 1] == 0) {
      continue;
    }
    rets.push_back(static_cast<double>(equity[i] - equity[i - 1]) /
                   static_cast<double>(equity[i - 1]));
  }
  if (rets.size() < 2) {
    return 0;
  }
  double mean = 0;
  for (auto r : rets) {
    mean += r;
  }
  mean /= static_cast<double>(rets.size());
  double var = 0;
  for (auto r : rets) {
    const auto d = r - mean;
    var += d * d;
  }
  var /= static_cast<double>(rets.size() - 1);
  if (var <= 0) {
    return 0;
  }
  return mean / std::sqrt(var);
}

std::int64_t utc_day(const BarEvent& bar) { return bar.timestamp.nanos() / kNanosPerDay; }

struct Book {
  Capital cash{};
  Quantity position{};
  Price avg_entry{};
  Capital entry_fees{};
  Timestamp entry_ts{};
  Capital realized{};
  Capital fees{};
  int fills{0};
  int round_trips{0};
  int wins{0};
  std::int64_t hold_ns_sum{0};
};

void apply_fill(Book& book, const FillEvent& fill) {
  book.cash = Capital::from_paise(book.cash.paise() + fill.net_cash_impact.paise());
  book.fees = Capital::from_paise(book.fees.paise() + fill.fees.paise());
  ++book.fills;
  if (fill.side == Side::Buy) {
    if (book.position.shares() == 0) {
      book.position = fill.filled_qty;
      book.avg_entry = fill.fill_price;
      book.entry_fees = fill.fees;
      book.entry_ts = fill.timestamp;
    } else {
      const auto old_n = book.avg_entry.paise() * book.position.shares();
      const auto add_n = fill.fill_price.paise() * fill.filled_qty.shares();
      const auto new_q = book.position.shares() + fill.filled_qty.shares();
      book.position = Quantity::from_shares(new_q);
      book.avg_entry = Price::from_paise((old_n + add_n) / new_q);
      book.entry_fees = Capital::from_paise(book.entry_fees.paise() + fill.fees.paise());
    }
    return;
  }
  const auto pnl = notional(fill.fill_price, fill.filled_qty).paise() -
                   notional(book.avg_entry, fill.filled_qty).paise() - fill.fees.paise() -
                   book.entry_fees.paise();
  book.realized = Capital::from_paise(book.realized.paise() + pnl);
  ++book.round_trips;
  if (pnl > 0) {
    ++book.wins;
  }
  if (book.entry_ts.nanos() != 0) {
    book.hold_ns_sum += fill.timestamp.nanos() - book.entry_ts.nanos();
  }
  book.position = {};
  book.avg_entry = {};
  book.entry_fees = {};
  book.entry_ts = {};
}

std::int64_t marked_equity(const Book& book, const BarEvent& bar, TradingMode mode) {
  std::int64_t eq = book.cash.paise();
  if (book.position.shares() == 0) {
    return eq;
  }
  const int leverage = (mode == TradingMode::Mis) ? 5 : 1;
  const auto loan = book.avg_entry.paise() * book.position.shares() * (leverage - 1) / leverage;
  return eq + bar.close.paise() * book.position.shares() - loan;
}

}  // namespace

BacktestResult BacktestRunner::run(DataSourceRegistry& registry, StrategyRegistry& strategies,
                                   const BacktestRequest& request) {
  auto strategy = strategies.create(request.strategy_name);
  IndicatorLibrary lib;
  StrategyConfig cfg = request.strategy;
  cfg.symbol_id = request.symbol_id;
  strategy->configure(cfg, lib);
  const auto mode = strategy->metadata().trading_mode;

  auto bars = registry.active_provider().historical_loader().load_bars(
      request.symbol_id, request.from, request.to, strategy->metadata().required_resolution);

  SimulatedExchange exchange;
  Book book{};
  book.cash = request.starting_capital;
  std::int64_t peak = book.cash.paise();
  std::int64_t max_dd = 0;
  std::vector<std::int64_t> equity;
  equity.reserve(bars.size());
  std::vector<OrderIntent> intents;
  intents.reserve(4);
  std::vector<LoggedFill> fills_log;

  SpscRing<BarEvent, 1024> ring;
  std::size_t next = 0;
  std::size_t processed = 0;
  bool have_last = false;
  BarEvent last{};
  std::vector<DailySnapshot> daily;
  std::int64_t day_realized0 = 0;
  std::int64_t day_fees0 = 0;
  int day_fills0 = 0;
  int day_trips0 = 0;
  int day_wins0 = 0;

  auto close_day = [&](const BarEvent& day_bar) {
    DailySnapshot snap{};
    snap.timestamp_ns = day_bar.timestamp.nanos();
    snap.realized_pnl_paise = book.realized.paise() - day_realized0;
    snap.fees_paise = book.fees.paise() - day_fees0;
    snap.eod_equity_paise = marked_equity(book, day_bar, mode);
    snap.fills = book.fills - day_fills0;
    snap.round_trips = book.round_trips - day_trips0;
    snap.wins = book.wins - day_wins0;
    daily.push_back(snap);
    day_realized0 = book.realized.paise();
    day_fees0 = book.fees.paise();
    day_fills0 = book.fills;
    day_trips0 = book.round_trips;
    day_wins0 = book.wins;
  };

  auto submit_and_apply = [&](const OrderIntent& intent, const BarEvent& px) {
    const auto fill =
        exchange.submit(intent, px, mode, book.cash, book.position, book.avg_entry);
    if (!fill) {
      return;
    }
    apply_fill(book, *fill);
    fills_log.push_back(LoggedFill{
        .timestamp_ns = fill->timestamp.nanos(),
        .side = fill->side,
        .price_paise = fill->fill_price.paise(),
        .qty = fill->filled_qty.shares(),
        .fees_paise = fill->fees.paise(),
    });
    strategy->on_fill(*fill);
  };

  auto flatten = [&](const BarEvent& px) {
    if (book.position.shares() <= 0) {
      return;
    }
    submit_and_apply(
        make_intent(StrategyId::from(0), request.symbol_id, Side::Sell, book.position), px);
  };

  while (processed < bars.size()) {
    while (next < bars.size() && ring.try_push(bars[next])) {
      ++next;
    }
    BarEvent bar{};
    if (!ring.try_pop(bar)) {
      break;
    }
    ++processed;

    if (have_last && mode == TradingMode::Mis && utc_day(bar) != utc_day(last)) {
      flatten(last);
      close_day(last);
    }

    lib.update(bar);
    PortfolioView view{.position = book.position, .cash = book.cash};
    intents.clear();
    strategy->on_bar(bar, view, intents);
    for (const auto& intent : intents) {
      submit_and_apply(intent, bar);
    }
    if (strategy->should_exit()) {
      flatten(bar);
    }

    const auto eq = marked_equity(book, bar, mode);
    equity.push_back(eq);
    peak = std::max(peak, eq);
    max_dd = std::max(max_dd, peak - eq);
    have_last = true;
    last = bar;
  }

  if (have_last) {
    flatten(last);
    if (!equity.empty()) {
      equity.back() = marked_equity(book, last, mode);
    }
    close_day(last);
  }

  BacktestResult result{};
  result.strategy_name = request.strategy_name;
  result.starting_capital_paise = request.starting_capital.paise();
  result.ending_equity_paise = equity.empty() ? book.cash.paise() : equity.back();
  result.realized_pnl_paise = book.realized.paise();
  result.fees_paise = book.fees.paise();
  result.max_drawdown_paise = max_dd;
  result.fills = book.fills;
  result.round_trips = book.round_trips;
  result.winning_round_trips = book.wins;
  result.win_rate = book.round_trips == 0 ? 0.0 : static_cast<double>(book.wins) / book.round_trips;
  result.sharpe = sharpe_from_equity(equity);
  result.avg_hold_seconds = book.round_trips == 0
                                ? 0.0
                                : static_cast<double>(book.hold_ns_sum) /
                                      static_cast<double>(book.round_trips * kNanosPerSecond);
  result.bars = bars.size();
  result.daily = std::move(daily);
  result.fills_log = std::move(fills_log);
  return result;
}

}  // namespace algocraft
