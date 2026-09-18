#pragma once

#include <cstdint>
#include <string_view>

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

inline std::string_view bar_resolution_code(BarResolution resolution) {
  switch (resolution) {
    case BarResolution::OneMin:
      return "1m";
    case BarResolution::FiveMin:
      return "5m";
    case BarResolution::FifteenMin:
      return "15m";
    case BarResolution::ThirtyMin:
      return "30m";
    case BarResolution::OneHour:
      return "1h";
    case BarResolution::OneDay:
      return "1d";
    case BarResolution::OneWeek:
      return "1w";
  }
  return "1m";
}

}  // namespace algocraft
