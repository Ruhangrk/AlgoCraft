#include "algocraft/backtest/backtest_runner.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft {
namespace {

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

}  // namespace

BacktestResult BacktestRunner::run(DataSourceRegistry& registry, StrategyRegistry& strategies,
                                   const BacktestRequest& request) {
  auto strategy = strategies.create(request.strategy_name);
  IndicatorLibrary lib;
  StrategyConfig cfg = request.strategy;
  cfg.symbol_id = request.symbol_id;
  strategy->configure(cfg, lib);

  auto bars = registry.active_provider().historical_loader().load_bars(
      request.symbol_id, request.from, request.to, strategy->metadata().required_resolution);

  SimulatedExchange exchange;
  Capital cash = request.starting_capital;
  Quantity position{};
  Price avg_entry{};
  Capital realized{};
  std::int64_t peak = cash.paise();
  std::int64_t max_dd = 0;
  std::vector<std::int64_t> equity;
  equity.reserve(bars.size());
  int fills = 0;
  int round_trips = 0;
  int wins = 0;

  for (const auto& bar : bars) {
    lib.update(bar);
    PortfolioView view{.position = position, .cash = cash};
    const auto intents = strategy->on_bar(bar, view);
    for (const auto& intent : intents) {
      const auto fill =
          exchange.submit(intent, bar, strategy->metadata().trading_mode, cash, position);
      if (!fill) {
        continue;
      }
      ++fills;
      if (fill->side == Side::Buy) {
        const auto value = notional(fill->fill_price, fill->filled_qty);
        cash = Capital::from_paise(cash.paise() - value.paise() / 5 - fill->fees.paise());
        position = fill->filled_qty;
        avg_entry = fill->fill_price;
      } else {
        const auto exit_value = notional(fill->fill_price, fill->filled_qty);
        const auto entry_value = notional(avg_entry, fill->filled_qty);
        const auto pnl = exit_value.paise() - entry_value.paise() - fill->fees.paise();
        cash = Capital::from_paise(cash.paise() + entry_value.paise() / 5 + pnl);
        realized = Capital::from_paise(realized.paise() + pnl);
        position = {};
        ++round_trips;
        if (pnl > 0) {
          ++wins;
        }
      }
      strategy->on_fill(*fill);
    }

    std::int64_t eq = cash.paise();
    if (position.shares() != 0) {
      const auto loan = avg_entry.paise() * position.shares() * 4 / 5;
      eq += bar.close.paise() * position.shares() - loan;
    }
    equity.push_back(eq);
    peak = std::max(peak, eq);
    max_dd = std::max(max_dd, peak - eq);
  }

  BacktestResult result{};
  result.strategy_name = request.strategy_name;
  result.starting_capital_paise = request.starting_capital.paise();
  result.ending_equity_paise = equity.empty() ? cash.paise() : equity.back();
  result.realized_pnl_paise = realized.paise();
  result.max_drawdown_paise = max_dd;
  result.fills = fills;
  result.round_trips = round_trips;
  result.winning_round_trips = wins;
  result.win_rate = round_trips == 0 ? 0.0 : static_cast<double>(wins) / round_trips;
  result.sharpe = sharpe_from_equity(equity);
  return result;
}

}  // namespace algocraft
