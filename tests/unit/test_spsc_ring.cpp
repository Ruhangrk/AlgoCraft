#include "algocraft/engine/dummy_event.hpp"
#include "algocraft/engine/spsc_ring.hpp"

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

using algocraft::DummyEvent;
using algocraft::SpscRing;

TEST(SpscRing, PushPopSameThread) {
  SpscRing<DummyEvent, 8> ring;
  DummyEvent in{.kind = 1, .symbol_id = 7, .seq = 42};
  ASSERT_TRUE(ring.try_push(in));

  DummyEvent out{};
  ASSERT_TRUE(ring.try_pop(out));
  EXPECT_EQ(out.kind, 1u);
  EXPECT_EQ(out.symbol_id, 7u);
  EXPECT_EQ(out.seq, 42u);
  EXPECT_FALSE(ring.try_pop(out));
}

TEST(SpscRing, FillsThenRejects) {
  SpscRing<DummyEvent, 4> ring;
  DummyEvent event{.seq = 1};
  std::size_t pushed = 0;
  while (ring.try_push(event)) {
    ++pushed;
  }
  EXPECT_GT(pushed, 0u);
  EXPECT_LE(pushed, 4u);
}

TEST(SpscRing, TwoThreadsTransferAll) {
  constexpr int kCount = 10'000;
  SpscRing<DummyEvent, 1024> ring;
  std::atomic<int> received{0};

  std::thread consumer([&] {
    DummyEvent event{};
    while (received.load(std::memory_order_relaxed) < kCount) {
      if (ring.try_pop(event)) {
        received.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      DummyEvent event{};
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
