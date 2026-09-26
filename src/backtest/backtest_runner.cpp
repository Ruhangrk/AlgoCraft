#include "algocraft/backtest/backtest_runner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "algocraft/container/trading_container.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/execution/simulated_exchange.hpp"
#include "algocraft/indicators/daily_sma_warmup.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/risk/risk_engine.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft {
namespace {

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;
constexpr std::int64_t kNanosPerSecond = 1'000'000'000LL;

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

std::int64_t marked_equity(const TradingContainer& c, const BarEvent& bar, TradingMode mode) {
  std::int64_t eq = c.cash().paise();
  if (c.position().shares() == 0) {
    return eq;
  }
  const int leverage = (mode == TradingMode::Mis) ? 5 : 1;
  const auto loan =
      c.avg_entry().paise() * c.position().shares() * (leverage - 1) / leverage;
  return eq + bar.close.paise() * c.position().shares() - loan;
}

}  // namespace

BacktestResult BacktestRunner::run(DataSourceRegistry& registry, StrategyRegistry& strategies,
                                   const BacktestRequest& request) {
  auto strategy = strategies.create(request.strategy_name);
  StrategyConfig cfg = request.strategy;
  cfg.symbol_id = request.symbol_id;
  const auto meta = strategy->metadata();
  const auto mode = meta.trading_mode;
  const auto resolution = meta.required_resolution;
  const bool need_sma = strategy_needs_daily_sma(meta);

  auto bars = registry.active_provider().historical_loader().load_bars(
      request.symbol_id, request.from, request.to, resolution);

  SimulatedExchange venue;
  RiskEngine risk;
  TradingContainerConfig ccfg{};
  ccfg.id = ContainerId::from(1);
  ccfg.workbook_id = request.workbook_id;
  ccfg.symbol_id = request.symbol_id;
  ccfg.strategy_id = StrategyId::from(1);
  ccfg.trading_mode = mode;
  ccfg.mode = ContainerMode::Backtest;
  ccfg.sim_cash = request.starting_capital;
  ccfg.real_allocation = request.starting_capital;
  ccfg.strategy_name = request.strategy_name;
  ccfg.strategy = cfg;

  TradingContainer container(std::move(ccfg), std::move(strategy), venue, risk, nullptr);
  // Seed daily SMA (DataFetch → Rocks ensure) before the 1m tape.
  if (need_sma) {
    auto daily = load_daily_sma_warmup(registry.active_provider().historical_loader(),
                                       request.symbol_id, request.from);
    if (!daily.empty()) {
      container.warmup(daily);
    }
  }
  const auto started = container.start();
  if (!started.ok) {
    BacktestResult empty{};
    empty.strategy_name = request.strategy_name;
    empty.starting_capital_paise = request.starting_capital.paise();
    empty.ending_equity_paise = request.starting_capital.paise();
    empty.bars = bars.size();
    return empty;
  }

  std::int64_t peak = request.starting_capital.paise();
  std::int64_t max_dd = 0;
  std::vector<std::int64_t> equity;
  equity.reserve(bars.size());
  std::vector<DailySnapshot> daily;
  std::int64_t day_realized0 = 0;
  std::int64_t day_fees0 = 0;
  int day_fills0 = 0;
  bool have_last = false;
  BarEvent last{};

  auto close_day = [&](const BarEvent& day_bar) {
    DailySnapshot snap{};
    snap.timestamp_ns = day_bar.timestamp.nanos();
    snap.realized_pnl_paise = container.realized().paise() - day_realized0;
    snap.fees_paise = container.fees().paise() - day_fees0;
    snap.eod_equity_paise = marked_equity(container, day_bar, mode);
    snap.fills = container.fills() - day_fills0;
    daily.push_back(snap);
    day_realized0 = container.realized().paise();
    day_fees0 = container.fees().paise();
    day_fills0 = container.fills();
  };

  for (const auto& bar : bars) {
    if (have_last && mode == TradingMode::Mis && utc_day(bar) != utc_day(last)) {
      SystemEvent sq{};
      sq.type = SystemEventType::MisSquareoffWarning;
      sq.timestamp = last.timestamp;
      container.on_system_event(sq);
      close_day(last);
      SystemEvent sess{};
      sess.type = SystemEventType::SessionStart;
      sess.timestamp = bar.timestamp;
      container.on_system_event(sess);
    }
    if (container.status() != ContainerStatus::Active) {
      break;
    }
    container.on_bar(bar);
    const auto eq = marked_equity(container, bar, mode);
    equity.push_back(eq);
    peak = std::max(peak, eq);
    max_dd = std::max(max_dd, peak - eq);
    have_last = true;
    last = bar;
  }

  if (have_last && container.status() == ContainerStatus::Active) {
    container.exit();
    if (!equity.empty()) {
      equity.back() = marked_equity(container, last, mode);
    }
    close_day(last);
  } else if (container.status() != ContainerStatus::Stopped) {
    container.stop();
  }

  int round_trips = 0;
  int wins = 0;
  std::int64_t hold_ns_sum = 0;
  std::int64_t entry_ts = 0;
  std::int64_t entry_fees = 0;
  std::int64_t avg_entry = 0;
  std::int64_t pos = 0;
  // Rebuild round-trip stats from fill events (container realized omits entry fees).
  std::int64_t realized_with_entry_fees = 0;
  for (const auto& fill : container.fill_events()) {
    if (fill.side == Side::Buy) {
      if (pos == 0) {
        pos = fill.filled_qty.shares();
        avg_entry = fill.fill_price.paise();
        entry_fees = fill.fees.paise();
        entry_ts = fill.timestamp.nanos();
      } else {
        const auto old_n = avg_entry * pos;
        const auto add_n = fill.fill_price.paise() * fill.filled_qty.shares();
        pos += fill.filled_qty.shares();
        avg_entry = (old_n + add_n) / pos;
        entry_fees += fill.fees.paise();
      }
    } else {
      const auto pnl = fill.fill_price.paise() * fill.filled_qty.shares() -
                       avg_entry * fill.filled_qty.shares() - fill.fees.paise() - entry_fees;
      realized_with_entry_fees += pnl;
      ++round_trips;
      if (pnl > 0) {
        ++wins;
      }
      if (entry_ts != 0) {
        hold_ns_sum += fill.timestamp.nanos() - entry_ts;
      }
      pos = 0;
      avg_entry = 0;
      entry_fees = 0;
      entry_ts = 0;
    }
  }

  BacktestResult result{};
  result.strategy_name = request.strategy_name;
  result.starting_capital_paise = request.starting_capital.paise();
  result.ending_equity_paise =
      equity.empty() ? container.cash().paise() : equity.back();
  result.realized_pnl_paise = realized_with_entry_fees;
  result.fees_paise = container.fees().paise();
  result.max_drawdown_paise = max_dd;
  result.fills = container.fills();
  result.round_trips = round_trips;
  result.winning_round_trips = wins;
  result.win_rate = round_trips == 0 ? 0.0 : static_cast<double>(wins) / round_trips;
  result.sharpe = sharpe_from_equity(equity);
  result.avg_hold_seconds = round_trips == 0
                                ? 0.0
                                : static_cast<double>(hold_ns_sum) /
                                      static_cast<double>(round_trips * kNanosPerSecond);
  result.bars = bars.size();
  result.daily = std::move(daily);
  result.fills_log.reserve(container.fill_events().size());
  for (const auto& fill : container.fill_events()) {
    result.fills_log.push_back(LoggedFill{
        .timestamp_ns = fill.timestamp.nanos(),
        .side = fill.side,
        .price_paise = fill.fill_price.paise(),
        .qty = fill.filled_qty.shares(),
        .fees_paise = fill.fees.paise(),
    });
  }
  result.signals.reserve(container.signals().size());
  for (const auto& s : container.signals()) {
    result.signals.push_back(LoggedSignal{
        .timestamp_ns = s.timestamp.nanos(),
        .intent_count = s.intent_count,
        .indicators_json = s.indicators_json,
    });
  }
  result.rejections.reserve(container.rejections().size());
  for (const auto& r : container.rejections()) {
    result.rejections.push_back(LoggedRejection{
        .timestamp_ns = r.timestamp.nanos(),
        .rule = r.rule,
        .reason = r.reason,
    });
  }
  return result;
}

}  // namespace algocraft
