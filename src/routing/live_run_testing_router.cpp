#include "algocraft/routing/live_run_testing_router.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/log/log.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/routing/position_sizer.hpp"

namespace algocraft {

void LiveRunTestingRouter::configure(const RoutingConfig& config) { config_ = config; }

RouterDefaults LiveRunTestingRouter::defaults() const {
  // 15 liquid names × live_run_testing; 1 session (last closed day); live top 3 by PnL.
  return RouterDefaults{
      .tickers = {"RELIANCE", "INFY", "TCS", "HDFCBANK", "ITC", "SBIN", "ONGC", "COALINDIA",
                  "TECHM", "WIPRO", "VEDL", "CANBK", "SAIL", "MPHASIS", "JUBLFOOD"},
      .strategies = {"live_run_testing"},
      .eval_sessions = 1,
  };
}

void LiveRunTestingRouter::start(DataSourceRegistry& data, StrategyRegistry& strategies,
                                 ContainerManager& containers) {
  AC_LOG_INFO("router_start name=live_run_testing_router stocks={} strategies={}",
              config_.stocks.size(), config_.strategies.size());
  evaluate_all(data, strategies);
  select_top_n(kTopN);

  std::vector<StrategyEvalResult*> winners;
  for (auto& ev : evaluations_) {
    if (ev.selected) {
      winners.push_back(&ev);
    }
  }
  if (winners.empty()) {
    AC_LOG_INFO("router_no_winners");
    return;
  }

  AC_LOG_INFO("router_winners n={}", winners.size());
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
    AC_LOG_DEBUG("router_create sid={} strategy={} alloc_paise={} pnl_paise={}", winner->symbol_id,
                 winner->strategy_name, alloc.paise(), winner->pnl_paise);
    (void)containers.create(req);
  }
}

void LiveRunTestingRouter::evaluate_all(DataSourceRegistry& data, StrategyRegistry& strategies) {
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
        AC_LOG_TRACE("router_eval skip empty sid={} strategy={}", symbol_id, name);
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
      AC_LOG_DEBUG("router_eval sid={} strategy={} bars={} fills={} pnl_paise={}", symbol_id, name,
                   row.bars, row.fills, row.pnl_paise);
      evaluations_.push_back(row);
    }
  }
}

void LiveRunTestingRouter::select_top_n(std::size_t n) {
  // Rank by PnL descending — highest profit or least loss. Require at least one bar.
  std::vector<std::size_t> idx;
  idx.reserve(evaluations_.size());
  for (std::size_t i = 0; i < evaluations_.size(); ++i) {
    if (evaluations_[i].bars > 0) {
      idx.push_back(i);
    }
  }
  std::sort(idx.begin(), idx.end(), [this](std::size_t a, std::size_t b) {
    return evaluations_[a].pnl_paise > evaluations_[b].pnl_paise;
  });
  if (idx.size() > n) {
    idx.resize(n);
  }
  for (const auto i : idx) {
    evaluations_[i].selected = true;
  }
  AC_LOG_DEBUG("router_select_top_n requested={} selected={}", n, idx.size());
}

}  // namespace algocraft
