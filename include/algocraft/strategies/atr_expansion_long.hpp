#pragma once
#include "algocraft/strategies/strategy.hpp"
#include <array>
namespace algocraft {
// Volatility: when true-range ≥ 1.8× avg of prior 14 TRs and close > open, buy. Exit −1×ATR or +1.5×ATR.
class AtrExpansionLong final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  std::array<double, 14> tr_{};
  int n_{0};
  double atr_{0};
  std::int64_t prev_close_{0};
  std::int64_t entry_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
