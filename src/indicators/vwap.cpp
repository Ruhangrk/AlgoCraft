#include "algocraft/indicators/vwap.hpp"

#include <cstdint>

namespace algocraft {
namespace {

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;

}  // namespace

void Vwap::update(const BarEvent& bar) {
  const auto day = bar.timestamp.nanos() / kNanosPerDay;
  if (day != session_day_) {
    session_day_ = day;
    pv_sum_ = 0;
    volume_sum_ = 0;
  }

  const auto typical = (static_cast<double>(bar.high.paise()) + static_cast<double>(bar.low.paise()) +
                        static_cast<double>(bar.close.paise())) /
                       3.0;
  const auto vol = static_cast<double>(bar.volume.shares());
  pv_sum_ += typical * vol;
  volume_sum_ += vol;
}

double Vwap::value() const {
  if (volume_sum_ <= 0) {
    return 0;
  }
  return pv_sum_ / volume_sum_;
}

}  // namespace algocraft
