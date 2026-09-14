#include "algocraft/engine/phase0_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

TEST(Phase0Runtime, FiveThreadsProcessDummyBarsAndShutdown) {
  algocraft::Phase0Runtime runtime;
  runtime.start();

  constexpr std::uint64_t kBars = 64;
  for (std::uint64_t i = 0; i < kBars; ++i) {
    algocraft::DummyEvent event{};
    event.kind = algocraft::kEventBar;
    event.symbol_id = 42;
    event.seq = i;
    while (!runtime.market_data_ring().try_push(event)) {
      std::this_thread::yield();
    }
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while ((runtime.bars_processed() < kBars || runtime.fills_processed() < kBars ||
          runtime.commands_processed() < kBars) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  runtime.stop();

  EXPECT_EQ(runtime.bars_processed(), kBars);
  EXPECT_EQ(runtime.fills_processed(), kBars);
  EXPECT_EQ(runtime.commands_processed(), kBars);
  EXPECT_GE(runtime.persist_events(), kBars);
  EXPECT_EQ(runtime.logs_written(), kBars);
}
