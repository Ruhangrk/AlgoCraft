#pragma once

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/ids.hpp"

namespace algocraft {

struct Position {
  SymbolId symbol_id{0};
  Quantity net_qty{};
  Price average_price{};
  Capital unrealized_pnl{};
  Capital realized_pnl{};
};

}  // namespace algocraft
