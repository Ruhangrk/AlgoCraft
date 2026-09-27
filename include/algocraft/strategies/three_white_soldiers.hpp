#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// Three white soldiers: 3 strong greens, each close > prior close. Exit on first red or −0.40%.
class ThreeWhiteSoldiers final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  int green_streak_{0};
  std::int64_t last_close_{0};
  std::int64_t entry_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
