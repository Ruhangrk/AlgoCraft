#pragma once

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

struct Fill {
  FillId fill_id{};
  OrderId order_id{};
  SymbolId symbol_id{0};
  Side side{Side::Buy};
  Quantity filled_qty{};
  Price fill_price{};
  Capital fees{};
  Timestamp timestamp{};
  WorkbookId workbook_id{};
  ContainerId container_id{};
};

}  // namespace algocraft
