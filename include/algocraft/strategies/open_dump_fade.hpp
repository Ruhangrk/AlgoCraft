#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
// After session open dump (first ~20m close ≤ −0.30% vs open) buy the fade; exit +0.40% / −0.30% or by 12:00.
class OpenDumpFade final : public Strategy {
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
  std::int64_t session_day_{-1};
  std::int64_t day_open_{0};
  int bars_today_{0};
  bool bought_today_{false};
  std::int64_t entry_paise_{0};
};
}  // namespace algocraft
