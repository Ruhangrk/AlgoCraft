#pragma once

#include <cstdint>

#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

class ConsecutiveUpClip final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary& lib) override;
  void on_bar(const BarEvent& bar, const PortfolioView& portfolio,
              std::vector<OrderIntent>& out) override;
  void on_fill(const FillEvent& fill) override;
  void on_order_update(const OrderUpdate& update) override { (void)update; }
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;

private:
  void reset_cycle();
  void reset_session();
  [[nodiscard]] Quantity clip_qty(Price px) const;
  [[nodiscard]] OrderIntent sell_all(Quantity pos) const;

  StrategyConfig config_{};
  std::int64_t session_day_{-1};
  bool have_prev_{false};
  std::int64_t prev_close_{0};
  int up_streak_{0};
  int recover_streak_{0};
  std::int64_t p0_paise_{0};
  std::int64_t min_close_since_entry_{0};
  bool dipped_since_clip_{false};
  bool add_disqualified_{false};
};

}  // namespace algocraft
