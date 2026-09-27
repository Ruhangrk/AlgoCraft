#pragma once
#include "algocraft/strategies/strategy.hpp"
#include <array>
namespace algocraft {
// Break above max(high of prior 5) → buy; exit below min(low of prior 3) or +0.50% from entry.
class FiveBarHighBreak final : public Strategy {
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
  std::array<std::int64_t, 5> highs_{};
  std::array<std::int64_t, 5> lows_{};
  int n_{0};
  std::int64_t entry_paise_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
