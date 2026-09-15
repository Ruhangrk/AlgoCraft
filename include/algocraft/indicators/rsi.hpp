#pragma once

#include "algocraft/indicators/indicator.hpp"

namespace algocraft {

class Rsi final : public Indicator {
public:
  static constexpr const char* kTypeName = "RSI";

  explicit Rsi(int period);

  void update(const BarEvent& bar) override;
  [[nodiscard]] double value() const override { return rsi_; }
  [[nodiscard]] bool ready() const override { return ready_; }

private:
  int period_{0};
  int count_{0};
  bool has_prev_{false};
  bool ready_{false};
  double prev_close_{0};
  double avg_gain_{0};
  double avg_loss_{0};
  double rsi_{0};
};

}  // namespace algocraft
