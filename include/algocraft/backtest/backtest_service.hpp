#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "algocraft/backtest/backtest_result.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/market_data/data_fetch_service.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/persistence/backtest_repository.hpp"
#include "algocraft/persistence/instrument_repository.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/strategies/strategy.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft {

struct ManualBacktestRequest {
  std::int64_t workbook_id{};
  std::string ticker;
  std::string strategy_name;
  Capital capital{Capital::from_paise(1'00'000'00)};
  Timestamp from{};
  Timestamp to{};
  StrategyConfig strategy{};
};

struct ManualBacktestOutcome {
  BacktestRow row{};
  BacktestResult result{};
};

// Ensure 1m → BacktestRunner → persist backtests row.
// Capital is simulation-only (UI value); does not borrow/return workbook available.
class BacktestService {
public:
  BacktestService(WorkbookRepository& workbooks, BacktestRepository& backtests,
                  DataSourceRegistry& data, DataFetchService& fetch, StrategyRegistry& strategies,
                  SymbolTable& symbols, InstrumentRepository* instruments = nullptr);

  [[nodiscard]] ManualBacktestOutcome run(const ManualBacktestRequest& request);

private:
  WorkbookRepository* workbooks_{nullptr};
  BacktestRepository* backtests_{nullptr};
  DataSourceRegistry* data_{nullptr};
  DataFetchService* fetch_{nullptr};
  StrategyRegistry* strategies_{nullptr};
  SymbolTable* symbols_{nullptr};
  InstrumentRepository* instruments_{nullptr};
};

}  // namespace algocraft
