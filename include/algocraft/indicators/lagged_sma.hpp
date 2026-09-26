#pragma once

#include <deque>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/indicators/indicator.hpp"

namespace algocraft {

// Average of the previous `period` closes (excludes the bar just applied).
// After update(), value() is the mean of the window *before* the latest close
// was pushed — so strategy can compare current close vs prior N after lib_.update.
// Only consumes OneMin bars (daily warmup will not pollute).
class LaggedSma final : public Indicator {
public:
  static constexpr const char* kTypeName = "LAGGED_SMA";

  explicit LaggedSma(int period);

  void update(const BarEvent& bar) override;
  [[nodiscard]] double value() const override { return prior_mean_; }
  [[nodiscard]] bool ready() const override { return ready_; }
  [[nodiscard]] int period() const { return period_; }

private:
  int period_{0};
  std::deque<double> window_{};
  double sum_{0};
  double prior_mean_{0};
  bool ready_{false};
};

}  // namespace algocraft
