#pragma once

#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

// Test churn strategy: buy when flat, sell on the next bar. High fill rate for live-path smoke.
class LiveRunTesting final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  StrategyConfig config_{};
};

}  // namespace algocraft
