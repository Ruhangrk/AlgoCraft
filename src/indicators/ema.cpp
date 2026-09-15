#include "algocraft/indicators/ema.hpp"

namespace algocraft {

Ema::Ema(int period) : period_{period}, k_{2.0 / (static_cast<double>(period) + 1.0)} {}

void Ema::update(const BarEvent& bar) {
  const auto px = static_cast<double>(bar.close.paise());
  if (count_ == 0) {
    ema_ = px;
  } else {
    ema_ = px * k_ + ema_ * (1.0 - k_);
  }
  ++count_;
}

}  // namespace algocraft
