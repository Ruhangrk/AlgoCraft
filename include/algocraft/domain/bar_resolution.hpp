#pragma once

#include <cstdint>

namespace algocraft {

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
