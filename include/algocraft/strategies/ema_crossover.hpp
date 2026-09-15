#pragma once

#include "algocraft/indicators/ema.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

class EmaCrossover final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  std::vector<OrderIntent> on_bar(const BarEvent& bar, const PortfolioView& portfolio) override;
  void on_fill(const FillEvent& fill) override;
  void on_order_update(const OrderUpdate& update) override { (void)update; }
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  StrategyConfig config_{};
  const Ema* fast_{nullptr};
  const Ema* slow_{nullptr};
  bool have_prev_{false};
  bool prev_fast_above_{false};
};

}  // namespace algocraft
