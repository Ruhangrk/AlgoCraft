#include "algocraft/indicators/sma.hpp"

namespace algocraft {

Sma::Sma(int period) : period_{period} {}

void Sma::update(const BarEvent& bar) {
  if (bar.resolution != BarResolution::OneDay || period_ <= 0) {
    return;
  }
  const auto px = static_cast<double>(bar.close.paise());
  window_.push_back(px);
  sum_ += px;
  if (static_cast<int>(window_.size()) > period_) {
    sum_ -= window_.front();
    window_.pop_front();
  }
  if (!window_.empty()) {
    mean_ = sum_ / static_cast<double>(window_.size());
  }
}

}  // namespace algocraft
