#pragma once

#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

// Hot-path order: integer ids only for ticker; workbook is a 16-byte Uuid.
struct Order {
  OrderId order_id{};
  SymbolId symbol_id{0};
  Side side{Side::Buy};
  OrderType type{OrderType::Market};
  TradingMode trading_mode{TradingMode::Mis};
  Quantity quantity{};
  Price limit_price{};
  WorkbookId workbook_id{};
  ContainerId container_id{};
  StrategyId strategy_id{};
  RoutingAlgoId routing_algo_id{};
  Timestamp created_at{};
};

}  // namespace algocraft
