#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// Buy after 3 consecutive red 1m bars; sell on 2 green or −0.40% from entry. 40% cash.
class ThreeRedBounce final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent& fill) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  std::int64_t prev_close_{0};
  int red_streak_{0};
  int green_streak_{0};
  std::int64_t entry_paise_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
