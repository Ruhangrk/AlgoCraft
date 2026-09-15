#include "algocraft/indicators/rsi.hpp"

#include <algorithm>

namespace algocraft {

Rsi::Rsi(int period) : period_{period} {}

void Rsi::update(const BarEvent& bar) {
  const auto close = static_cast<double>(bar.close.paise());
  if (!has_prev_) {
    prev_close_ = close;
    has_prev_ = true;
    return;
  }

  const auto change = close - prev_close_;
  const auto gain = std::max(change, 0.0);
  const auto loss = std::max(-change, 0.0);
  prev_close_ = close;

  if (count_ < period_) {
    avg_gain_ += gain;
    avg_loss_ += loss;
    ++count_;
    if (count_ == period_) {
      avg_gain_ /= static_cast<double>(period_);
      avg_loss_ /= static_cast<double>(period_);
      ready_ = true;
    }
  } else {
    avg_gain_ = (avg_gain_ * (period_ - 1) + gain) / static_cast<double>(period_);
    avg_loss_ = (avg_loss_ * (period_ - 1) + loss) / static_cast<double>(period_);
  }

  if (!ready_) {
    return;
  }
  if (avg_loss_ == 0) {
    rsi_ = 100;
    return;
  }
  const auto rs = avg_gain_ / avg_loss_;
  rsi_ = 100.0 - 100.0 / (1.0 + rs);
}

}  // namespace algocraft
