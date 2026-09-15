#pragma once

#include <cstdint>

namespace algocraft {

// Candle size. Phase 0/1 only use OneMin; the rest is reserved.
enum class BarResolution : std::uint8_t {
  OneMin = 0,
  FiveMin,
  FifteenMin,
  ThirtyMin,
  OneHour,
  OneDay,
  OneWeek,
};

}  // namespace algocraft
