#pragma once

#include "algocraft/domain/price.hpp"
#include "algocraft/strategies/strategy.hpp"

#include <cstdint>

namespace algocraft {

class OpeningRangeBreakout final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent& fill) override;
  void on_order_update(const OrderUpdate& update) override { (void)update; }
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  StrategyConfig config_{};
  int bars_seen_{0};
  bool range_ready_{false};
  bool traded_{false};
  std::int64_t session_day_{-1};
  Price range_high_{};
  Price range_low_{};
};

}  // namespace algocraft
