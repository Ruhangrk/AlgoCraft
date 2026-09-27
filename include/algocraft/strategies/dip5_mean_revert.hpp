#pragma once

#include "algocraft/indicators/lagged_sma.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

// Mean-reversion (opposite of reliance_prev5 breakout):
// Buy when flat: close <= −0.25% vs avg(prev 5). Size = 40% cash.
// Sell all: close back to/above avg, OR hard stop −0.50% vs avg.
// No buys after 14:30 IST. Fixed params — no tuning knobs.
class Dip5MeanRevert final : public Strategy {
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
