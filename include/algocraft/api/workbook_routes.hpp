#pragma once

#include "algocraft/api/http_helpers.hpp"
#include "algocraft/auth/auth_service.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/data_fetch_service.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/backtest_repository.hpp"
#include "algocraft/persistence/instrument_repository.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft::api {

struct WorkbookRouteDeps {
  AuthService& auth;
  WorkbookRepository& workbooks;
  ActivityRepository& activity;
  WorkbookManager& books;
  DataSourceRegistry& data;
  StrategyRegistry& strategies;
  SymbolTable& symbols;
  DataFetchService* fetch{nullptr};
  BacktestRepository* backtests{nullptr};
  InstrumentRepository* instruments{nullptr};
};

void register_workbook_routes(App& app, WorkbookRouteDeps deps);

}  // namespace algocraft::api
