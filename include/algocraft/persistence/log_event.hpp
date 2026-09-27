#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace algocraft {

// Ordered least → most severe. Choosing a level shows that level and above.
enum class LogLevel : std::uint8_t {
  Trace = 0,
  Debug,
  Info,
  Warn,
  Error,
};

// Fixed-size so it can sit on a log queue / SPSC ring (no std::string).
struct LogEvent {
  std::int64_t timestamp_ns{0};
  LogLevel level{LogLevel::Info};
  std::uint16_t length{0};
  std::array<char, 256> message{};
};

static_assert(std::is_trivially_copyable_v<LogEvent>);

[[nodiscard]] inline std::string_view to_string(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
  }
  return "info";
}

// Recognizes: trace, debug, info, warn, warning, error, err.
[[nodiscard]] inline std::optional<LogLevel> parse_log_level(std::string_view s) {
  if (s == "trace") {
    return LogLevel::Trace;
  }
  if (s == "debug") {
    return LogLevel::Debug;
  }
  if (s == "info") {
    return LogLevel::Info;
  }
  if (s == "warn" || s == "warning") {
    return LogLevel::Warn;
  }
  if (s == "error" || s == "err") {
    return LogLevel::Error;
  }
  return std::nullopt;
}

}  // namespace algocraft
