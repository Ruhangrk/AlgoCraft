#include "algocraft/engine/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

#include <gtest/gtest.h>

using algocraft::SpscRing;

struct TestEvent {
  std::uint32_t kind{0};
  std::uint32_t symbol_id{0};
  std::uint64_t seq{0};
};

TEST(SpscRing, PushPopSameThread) {
  SpscRing<TestEvent, 8> ring;
  TestEvent in{.kind = 1, .symbol_id = 7, .seq = 42};
  ASSERT_TRUE(ring.try_push(in));

  TestEvent out{};
  ASSERT_TRUE(ring.try_pop(out));
  EXPECT_EQ(out.kind, 1u);
  EXPECT_EQ(out.symbol_id, 7u);
  EXPECT_EQ(out.seq, 42u);
  EXPECT_FALSE(ring.try_pop(out));
}

TEST(SpscRing, FillsThenRejects) {
  SpscRing<TestEvent, 4> ring;
  TestEvent event{.seq = 1};
  std::size_t pushed = 0;
  while (ring.try_push(event)) {
    ++pushed;
  }
  EXPECT_GT(pushed, 0u);
  EXPECT_LE(pushed, 4u);
}

TEST(SpscRing, TwoThreadsTransferAll) {
  constexpr int kCount = 10'000;
  SpscRing<TestEvent, 1024> ring;
  std::atomic<int> received{0};

  std::thread consumer([&] {
    TestEvent event{};
    while (received.load(std::memory_order_relaxed) < kCount) {
      if (ring.try_pop(event)) {
        received.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      TestEvent event{};
      event.seq = static_cast<std::uint64_t>(i);
      while (!ring.try_push(event)) {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();
  EXPECT_EQ(received.load(), kCount);
}
