#pragma once

#include <type_traits>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

// OrderOutRing payload: everything SimulatedExchange::submit needs, plus routing ids.
struct LiveOrderRequest {
  OrderIntent intent{};
  BarEvent bar{};
  TradingMode mode{TradingMode::Mis};
  Capital cash{};
  Quantity position{};
  Price avg_entry{};
  ContainerId container_id{};
  WorkbookId workbook_id{};
};

static_assert(std::is_trivially_copyable_v<LiveOrderRequest>);
static_assert(std::is_trivially_copyable_v<FillEvent>);

}  // namespace algocraft
