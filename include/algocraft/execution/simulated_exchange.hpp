#pragma once

#include "algocraft/execution/cost_calculator.hpp"
#include "algocraft/execution/execution_venue.hpp"

namespace algocraft {

class SimulatedExchange final : public ExecutionVenue {
public:
  explicit SimulatedExchange(CostCalculator costs = CostCalculator{},
                             Price slippage = Price::from_paise(1), int mis_leverage = 5);

  std::optional<FillEvent> submit(const OrderIntent& intent, const BarEvent& bar, TradingMode mode,
                                  Capital cash, Quantity position, Price avg_entry = {}) override;

private:
  CostCalculator costs_{};
  Price slippage_{};
  int mis_leverage_{5};
  std::uint64_t next_order_id_{1};
};

}  // namespace algocraft
