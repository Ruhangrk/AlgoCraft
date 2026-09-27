#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// Two consecutive green with 2nd body ≥ 1.5× 1st → buy; sell on first red or −0.35% from entry.
class TwoGreenThrust final : public Strategy {
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
  std::int64_t prev_o_{0}, prev_c_{0};
  bool have_prev_{false};
  std::int64_t entry_paise_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
