#pragma once

#include "algocraft/indicators/indicator.hpp"

namespace algocraft {

class Ema final : public Indicator {
public:
  static constexpr const char* kTypeName = "EMA";

  explicit Ema(int period);

  void update(const BarEvent& bar) override;
  [[nodiscard]] double value() const override { return ema_; }
  [[nodiscard]] bool ready() const override { return count_ >= period_; }

private:
  int period_{0};
  int count_{0};
  double k_{0};
  double ema_{0};
};

}  // namespace algocraft
