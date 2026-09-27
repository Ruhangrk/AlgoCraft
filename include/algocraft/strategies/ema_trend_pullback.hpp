#pragma once
#include "algocraft/indicators/ema.hpp"
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// EMA9 > EMA21 trend; buy pullback to EMA9 (±0.08%); exit EMA9 cross under EMA21 or −0.35% from entry.
class EmaTrendPullback final : public Strategy {
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
  Ema* fast_{nullptr};
  Ema* slow_{nullptr};
  std::int64_t entry_paise_{0};
};
}  // namespace algocraft
