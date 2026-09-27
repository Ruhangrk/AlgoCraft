#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/fmt/fmt.h>

#include "algocraft/log/log_hub.hpp"
#include "algocraft/persistence/log_event.hpp"

namespace algocraft::log {

void set_level(LogLevel level);
[[nodiscard]] LogLevel level();

// When non-null, write() enqueues to the hub (async drain → spdlog).
// When null, write() goes straight to spdlog (startup / tests).
void set_hub(LogHub* hub);
[[nodiscard]] LogHub* hub();

void apply_spdlog_level(LogLevel level);
void emit_direct(const LogEvent& event);

// Console + one file per calendar day under log_dir (creates dir if needed).
// Files: {log_dir}/algocraft_YYYY-MM-DD.log  (rollover at local midnight).
// Call once at process start, before set_hub.
void init_sinks(std::string_view log_dir = "data/logs");
[[nodiscard]] const std::string& log_dir();

// Format into a fixed buffer and enqueue or print. Never blocks the caller.
template <typename... Args>
void write(LogLevel lvl, fmt::format_string<Args...> fmt_str, Args&&... args) {
  if (static_cast<std::uint8_t>(lvl) < static_cast<std::uint8_t>(level())) {
    return;
  }
  LogEvent event{};
  event.level = lvl;
  auto out = fmt::format_to_n(event.message.data(), event.message.size(), fmt_str,
                              std::forward<Args>(args)...);
  const auto n = static_cast<std::size_t>(out.size);
  event.length =
      static_cast<std::uint16_t>(n > event.message.size() ? event.message.size() : n);

  if (LogHub* h = hub()) {
    (void)h->try_enqueue(event);
    return;
  }
  emit_direct(event);
}

}  // namespace algocraft::log

#define AC_LOG_TRACE(...) ::algocraft::log::write(::algocraft::LogLevel::Trace, __VA_ARGS__)
#define AC_LOG_DEBUG(...) ::algocraft::log::write(::algocraft::LogLevel::Debug, __VA_ARGS__)
#define AC_LOG_INFO(...) ::algocraft::log::write(::algocraft::LogLevel::Info, __VA_ARGS__)
#define AC_LOG_WARN(...) ::algocraft::log::write(::algocraft::LogLevel::Warn, __VA_ARGS__)
#define AC_LOG_ERROR(...) ::algocraft::log::write(::algocraft::LogLevel::Error, __VA_ARGS__)
