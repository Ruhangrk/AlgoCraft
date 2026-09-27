#include "algocraft/log/log.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace algocraft::log {
namespace {

std::atomic<std::uint8_t> g_level{static_cast<std::uint8_t>(LogLevel::Info)};
LogHub* g_hub{nullptr};
std::string g_log_dir{"data/logs"};
std::once_flag g_sinks_once;

spdlog::level::level_enum to_spdlog(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return spdlog::level::trace;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Error:
      return spdlog::level::err;
  }
  return spdlog::level::info;
}

}  // namespace

void set_level(LogLevel level) {
  g_level.store(static_cast<std::uint8_t>(level), std::memory_order_relaxed);
  apply_spdlog_level(level);
}

LogLevel level() {
  return static_cast<LogLevel>(g_level.load(std::memory_order_relaxed));
}

void set_hub(LogHub* hub) { g_hub = hub; }

LogHub* hub() { return g_hub; }

void apply_spdlog_level(LogLevel lvl) {
  spdlog::set_level(to_spdlog(lvl));
  spdlog::flush_on(spdlog::level::warn);
}

void emit_direct(const LogEvent& event) {
  const std::string_view text(event.message.data(), event.length);
  spdlog::log(to_spdlog(event.level), "{}", text);
}

void init_sinks(std::string_view log_dir) {
  std::call_once(g_sinks_once, [&] {
    g_log_dir = std::string{log_dir.empty() ? "data/logs" : log_dir};
    std::error_code ec;
    std::filesystem::create_directories(g_log_dir, ec);

    const auto file_base = (std::filesystem::path(g_log_dir) / "algocraft.log").string();

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    // Rollover at local 00:00 → one file per day: algocraft_YYYY-MM-DD.log
    sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(file_base, 0, 0));

    auto logger = std::make_shared<spdlog::logger>("algocraft", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(std::move(logger));
    apply_spdlog_level(level());
  });
}

const std::string& log_dir() { return g_log_dir; }

}  // namespace algocraft::log
