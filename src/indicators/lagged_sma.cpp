#include "algocraft/indicators/lagged_sma.hpp"

namespace algocraft {

LaggedSma::LaggedSma(int period) : period_{period} {}

void LaggedSma::update(const BarEvent& bar) {
  if (bar.resolution != BarResolution::OneMin || period_ <= 0) {
    return;
  }
  if (static_cast<int>(window_.size()) >= period_) {
    prior_mean_ = sum_ / static_cast<double>(period_);
    ready_ = true;
  }
  const auto px = static_cast<double>(bar.close.paise());
  window_.push_back(px);
  sum_ += px;
  if (static_cast<int>(window_.size()) > period_) {
    sum_ -= window_.front();
    window_.pop_front();
  }
}

}  // namespace algocraft
