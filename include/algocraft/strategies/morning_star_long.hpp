#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// Morning star: red, small-body, green closing into first half of red. TP/stop.
class MorningStarLong final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  std::int64_t o1_{0},c1_{0},o2_{0},c2_{0};
  int n_{0};
  std::int64_t entry_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
