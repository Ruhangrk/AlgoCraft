#include "algocraft/routing/default_router.hpp"

#include <cstdint>
#include <vector>

#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/indicators/daily_sma_warmup.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/routing/position_sizer.hpp"

namespace algocraft {

void DefaultRouter::configure(const RoutingConfig& config) { config_ = config; }

RouterDefaults DefaultRouter::defaults() const {
  // Same universe as CLI run / Upstox NSE EQ map.
  return RouterDefaults{
      .tickers = {"RELIANCE", "INFY", "TCS", "HDFCBANK", "ICICIBANK", "SBIN", "BHARTIARTL", "ITC",
                  "LT", "HINDUNILVR"},
      .strategies = {"ema_crossover", "vwap_reversion", "consecutive_up_clip"},
      .eval_sessions = 14,
  };
}

void DefaultRouter::start(DataSourceRegistry& data, StrategyRegistry& strategies,
                          ContainerManager& containers) {
  evaluate_all(data, strategies);

  std::vector<StrategyEvalResult*> winners;
  for (auto& ev : evaluations_) {
    if (ev.selected) {
      winners.push_back(&ev);
    }
  }
  if (winners.empty()) {
    return;
  }

  const auto n = static_cast<std::int64_t>(winners.size());
  const auto pool = containers.available_capital().paise();
  const auto base = pool / n;
  auto rem = pool % n;
  for (auto* winner : winners) {
    const auto alloc = Capital::from_paise(base + rem);
    rem = 0;
    ContainerManager::CreateRequest req{};
    req.symbol_id = winner->symbol_id;
    req.strategy_id = winner->strategy_id;
    req.strategy_name = winner->strategy_name;
    req.allocation = alloc;
    req.mode = ContainerMode::Real;
    req.last_price = winner->last_price;
    // Seed daily SMA via DataFetch (Rocks ensure) before live/1m trading starts.
    {
      auto probe = strategies.create(winner->strategy_name);
      if (probe && strategy_needs_daily_sma(probe->metadata())) {
        const auto as_of = config_.to.nanos() != 0 ? config_.to : Timestamp::now();
        req.warmup_bars = load_daily_sma_warmup(data.active_provider().historical_loader(),
                                                winner->symbol_id, as_of);
      }
    }
    (void)containers.create(req);
  }
}

void DefaultRouter::evaluate_all(DataSourceRegistry& data, StrategyRegistry& strategies) {
  evaluations_.clear();
  BacktestRunner runner;
  std::uint64_t strategy_seq = 1;
  for (const auto& name : config_.strategies) {
    const auto sid = StrategyId::from(strategy_seq++);
    for (const auto symbol_id : config_.stocks) {
      StrategyEvalResult row{};
      row.symbol_id = symbol_id;
      row.strategy_name = name;
      row.strategy_id = sid;

      auto bars = data.active_provider().historical_loader().load_bars(
          symbol_id, config_.from, config_.to, BarResolution::OneMin);
      row.bars = bars.size();
      if (bars.empty()) {
        evaluations_.push_back(row);
        continue;
      }
      row.last_price = bars.back().close;

      BacktestRequest req{};
      req.symbol_id = symbol_id;
      req.from = config_.from;
      req.to = config_.to;
      req.strategy_name = name;
      req.starting_capital = config_.eval_capital;
      req.strategy.order_qty = position_for_capital(config_.eval_capital, row.last_price);
      req.strategy.symbol_id = symbol_id;
      req.strategy.clip_paise = 20'00'000'00;
      req.strategy.alloc_paise = config_.eval_capital.paise();
      const auto result = runner.run(data, strategies, req);
      row.pnl_paise = result.realized_pnl_paise;
      row.fills = result.fills;
      row.selected = result.realized_pnl_paise > 0;
      evaluations_.push_back(row);
    }
  }
}

}  // namespace algocraft
