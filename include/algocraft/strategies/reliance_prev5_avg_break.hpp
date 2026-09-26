#pragma once

#include "algocraft/indicators/lagged_sma.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

// 1m long-only (no stacking):
// Buy when flat: close >= +0.30% vs avg(prev 5 closes); size = 50% of cash.
// Sell all: close <= −0.45% vs avg (stop) OR close >= +0.55% vs avg (take-profit).
// No new buys after 14:30 IST.
class ReliancePrev5AvgBreak final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent& fill) override { (void)fill; }
  void on_order_update(const OrderUpdate& update) override { (void)update; }
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  [[nodiscard]] Quantity buy_clip(Price px, Capital available) const;

  StrategyConfig config_{};
  LaggedSma* avg5_{nullptr};
};

}  // namespace algocraft
