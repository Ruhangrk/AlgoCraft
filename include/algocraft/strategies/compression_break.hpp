#pragma once
#include "algocraft/strategies/strategy.hpp"
#include <array>
namespace algocraft {
// 4 tiny-range bars then close breaks +0.20% above their high → buy; exit −0.30% or +0.40% from entry.
class CompressionBreak final : public Strategy {
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
  std::array<std::int64_t, 4> highs_{};
  std::array<std::int64_t, 4> lows_{};
  int n_{0};
  std::int64_t entry_paise_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
