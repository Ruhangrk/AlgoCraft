#pragma once

#include "algocraft/api/http_helpers.hpp"
#include "algocraft/auth/auth_service.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/data_fetch_service.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft::api {

struct MarketRouteDeps {
  AuthService& auth;
  StrategyRegistry& strategies;
  DataSourceRegistry& data;
  SymbolTable& symbols;
  DataFetchService* fetch{nullptr};
};

void register_market_routes(App& app, MarketRouteDeps deps);

}  // namespace algocraft::api
