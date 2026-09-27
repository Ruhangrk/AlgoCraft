#pragma once
#include "algocraft/indicators/vwap.hpp"
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// Buy when close crosses up through VWAP; sell when close crosses back below. 40% cash.
class VwapReclaimLong final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent&) override {}
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  Vwap* vwap_{nullptr};
  bool have_prev_{false};
  bool prev_below_{false};
};
}  // namespace algocraft
