#pragma once

#include <optional>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

class ExecutionVenue {
public:
  virtual ~ExecutionVenue() = default;

  virtual std::optional<FillEvent> submit(const OrderIntent& intent, const BarEvent& bar,
                                          TradingMode mode, Capital cash,
                                          Quantity position) = 0;
};

}  // namespace algocraft
