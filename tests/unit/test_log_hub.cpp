#include "algocraft/log/log.hpp"
#include "algocraft/log/log_hub.hpp"
#include "algocraft/persistence/log_event.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

TEST(LogLevel, ParseNames) {
  EXPECT_EQ(algocraft::parse_log_level("trace"), algocraft::LogLevel::Trace);
  EXPECT_EQ(algocraft::parse_log_level("debug"), algocraft::LogLevel::Debug);
  EXPECT_EQ(algocraft::parse_log_level("info"), algocraft::LogLevel::Info);
  EXPECT_EQ(algocraft::parse_log_level("warn"), algocraft::LogLevel::Warn);
  EXPECT_EQ(algocraft::parse_log_level("warning"), algocraft::LogLevel::Warn);
  EXPECT_EQ(algocraft::parse_log_level("error"), algocraft::LogLevel::Error);
  EXPECT_FALSE(algocraft::parse_log_level("nope").has_value());
}

TEST(LogHub, InitSinksCreatesDailyDir) {
  // Must run before other tests that might call init_sinks (call_once).
  const auto dir = std::filesystem::temp_directory_path() / "algocraft_log_test";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  algocraft::log::init_sinks(dir.string());
  EXPECT_TRUE(std::filesystem::is_directory(dir));
  EXPECT_EQ(algocraft::log::log_dir(), dir.string());
  AC_LOG_INFO("file_sink_smoke");
  spdlog::default_logger()->flush();

  bool found = false;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const auto name = entry.path().filename().string();
    if (name.rfind("algocraft_", 0) == 0 && name.find(".log") != std::string::npos) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST(LogHub, EnqueueAndDrain) {
  algocraft::LogHub hub;
  algocraft::log::set_level(algocraft::LogLevel::Trace);
  hub.start();
  algocraft::log::set_hub(&hub);

  AC_LOG_INFO("hub_test_msg={}", 42);
  AC_LOG_TRACE("hub_trace={}", "x");

  for (int i = 0; i < 50 && hub.written() < 2; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_GE(hub.written(), 2u);

  algocraft::log::set_hub(nullptr);
  hub.stop();
  algocraft::log::set_level(algocraft::LogLevel::Info);
}

TEST(LogHub, RespectsLevel) {
  algocraft::log::set_hub(nullptr);
  algocraft::log::set_level(algocraft::LogLevel::Warn);
  AC_LOG_DEBUG("filtered");
  AC_LOG_INFO("filtered_info");
  algocraft::log::set_level(algocraft::LogLevel::Info);
}
