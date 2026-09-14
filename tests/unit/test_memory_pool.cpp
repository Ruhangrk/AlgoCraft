#include "algocraft/engine/memory_pool.hpp"

#include <gtest/gtest.h>

using algocraft::MemoryPool;

TEST(MemoryPool, AcquireUntilEmptyThenRelease) {
  MemoryPool<int> pool(3);
  EXPECT_EQ(pool.capacity(), 3u);
  EXPECT_EQ(pool.available(), 3u);

  int* a = pool.acquire();
  int* b = pool.acquire();
  int* c = pool.acquire();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(pool.acquire(), nullptr);
  EXPECT_EQ(pool.available(), 0u);

  *a = 11;
  pool.release(a);
  int* again = pool.acquire();
  ASSERT_EQ(again, a);
  EXPECT_EQ(*again, 11);
}

TEST(MemoryPool, NullReleaseIsSafe) {
  MemoryPool<int> pool(1);
  pool.release(nullptr);
  EXPECT_EQ(pool.available(), 1u);
}
