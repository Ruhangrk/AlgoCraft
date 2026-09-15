#pragma once

#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

inline OrderIntent make_intent(StrategyId strategy_id, SymbolId symbol_id, Side side,
                               Quantity qty, Price price = {}) {
  OrderIntent intent{};
  intent.strategy_id = strategy_id;
  intent.symbol_id = symbol_id;
  intent.side = side;
  intent.quantity = qty;
  intent.type = OrderType::Market;
  intent.price = price;
  return intent;
}

}  // namespace algocraft
