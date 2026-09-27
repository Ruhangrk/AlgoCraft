#pragma once
#include "algocraft/strategies/strategy.hpp"
#include <array>
namespace algocraft {
// NR7 (narrowest range of last 7) then break above NR7 high → buy. Volatility compression → expansion.
class Nr7Breakout final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  std::array<std::int64_t, 7> highs_{};
  std::array<std::int64_t, 7> lows_{};
  int n_{0};
  bool armed_{false};
  std::int64_t nr_high_{0}, nr_low_{0};
  std::int64_t entry_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
