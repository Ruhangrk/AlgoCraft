#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// Bullish harami (small green inside prior red body); buy only on next bar break of mother high.
class BullHaramiBreak final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override;
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
  std::int64_t mother_high_{0}, mother_low_{0};
  bool armed_{false};
  std::int64_t po_{0}, pc_{0};
  bool have_{false};
  std::int64_t entry_{0};
  std::int64_t session_day_{-1};
};
}  // namespace algocraft
