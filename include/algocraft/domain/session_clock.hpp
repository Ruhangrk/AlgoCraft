#pragma once

#include <cstdint>

#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

constexpr std::int64_t kIstOffsetNs = 19'800'000'000'000LL;
constexpr std::int64_t kNanosPerDay = 86'400'000'000'000LL;
constexpr std::int64_t kNanosPerMinute = 60'000'000'000LL;

inline int ist_minute_of_day(Timestamp ts) {
  const auto ist = ts.nanos() + kIstOffsetNs;
  auto day = ist % kNanosPerDay;
  if (day < 0) {
    day += kNanosPerDay;
  }
  return static_cast<int>(day / kNanosPerMinute);
}

inline bool nse_regular_hours(Timestamp ts) {
  const auto m = ist_minute_of_day(ts);
  return m >= 9 * 60 + 15 && m < 15 * 60 + 30;
}

}  // namespace algocraft
