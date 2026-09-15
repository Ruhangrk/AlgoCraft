#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace algocraft {

enum class LogLevel : std::uint8_t {
  Debug = 0,
  Info,
  Warn,
  Error,
};

// Fixed-size so it can sit on an SPSC ring (no std::string).
struct LogEvent {
  std::int64_t timestamp_ns{0};
  LogLevel level{LogLevel::Info};
  std::uint16_t length{0};
  std::array<char, 192> message{};
};

static_assert(std::is_trivially_copyable_v<LogEvent>);

}  // namespace algocraft
