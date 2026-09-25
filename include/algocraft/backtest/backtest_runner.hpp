#pragma once

#include "algocraft/backtest/backtest_result.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/strategies/strategy.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft {

struct BacktestRequest {
  SymbolId symbol_id{0};
  WorkbookId workbook_id{};
  Timestamp from{};
  Timestamp to{};
  Capital starting_capital{Capital::from_paise(1'00'000'00)};
  StrategyConfig strategy;
  std::string strategy_name;
};

// Replays bars through a BACKTEST TradingContainer (no workbook capital borrow).
// Risk + SimulatedExchange match the live container path. Events stay on the result.
class BacktestRunner {
public:
  BacktestResult run(DataSourceRegistry& registry, StrategyRegistry& strategies,
                     const BacktestRequest& request);
};

}  // namespace algocraft
