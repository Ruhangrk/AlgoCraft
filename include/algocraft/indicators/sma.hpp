#pragma once

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/indicators/indicator.hpp"

#include <deque>

namespace algocraft {

// Simple moving average of daily closes. Ignores non-OneDay bars so 1m tape
// does not pollute the window (seed via warmup with daily bars).
class Sma final : public Indicator {
public:
  static constexpr const char* kTypeName = "SMA";

  explicit Sma(int period);

  void update(const BarEvent& bar) override;
  [[nodiscard]] double value() const override { return mean_; }
  [[nodiscard]] bool ready() const override {
    return static_cast<int>(window_.size()) >= period_;
  }
  [[nodiscard]] int period() const { return period_; }

private:
  int period_{0};
  std::deque<double> window_{};
  double sum_{0};
  double mean_{0};
};

}  // namespace algocraft
