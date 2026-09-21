#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace algocraft {

// Candle size. Engine path uses OneMin; chart path uses OneDay / OneWeek / OneMonth.
enum class BarResolution : std::uint8_t {
  OneMin = 0,
  FiveMin,
  FifteenMin,
  ThirtyMin,
  OneHour,
  OneDay,
  OneWeek,
  OneMonth,
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
    case BarResolution::OneMonth:
      return "1M";
  }
  return "1m";
}

// Chart-only TFs (D/W/M). Not used to drive strategy on_bar.
inline bool is_chart_resolution(BarResolution resolution) {
  return resolution == BarResolution::OneDay || resolution == BarResolution::OneWeek ||
         resolution == BarResolution::OneMonth;
}

inline std::optional<BarResolution> bar_resolution_from_code(std::string_view code) {
  if (code == "1m") {
    return BarResolution::OneMin;
  }
  if (code == "5m") {
    return BarResolution::FiveMin;
  }
  if (code == "15m") {
    return BarResolution::FifteenMin;
  }
  if (code == "30m") {
    return BarResolution::ThirtyMin;
  }
  if (code == "1h") {
    return BarResolution::OneHour;
  }
  if (code == "1d") {
    return BarResolution::OneDay;
  }
  if (code == "1w") {
    return BarResolution::OneWeek;
  }
  if (code == "1M") {
    return BarResolution::OneMonth;
  }
  return std::nullopt;
}

}  // namespace algocraft
