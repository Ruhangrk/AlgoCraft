#include <gtest/gtest.h>

#include "algocraft/engine/live_run_service.hpp"

TEST(StatusSseHub, PushStoresLatestGeneration) {
  algocraft::StatusSseHub hub;
  EXPECT_FALSE(hub.latest(1, algocraft::StatusSseHub::Channel::Portfolio).has_value());

  hub.push(1, algocraft::StatusSseHub::Channel::Portfolio, R"({"workbook_id":1})");
  const auto a = hub.latest(1, algocraft::StatusSseHub::Channel::Portfolio);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->generation, 1u);
  EXPECT_EQ(a->json, R"({"workbook_id":1})");

  hub.push(1, algocraft::StatusSseHub::Channel::Portfolio, R"({"workbook_id":1,"v":2})");
  const auto b = hub.latest(1, algocraft::StatusSseHub::Channel::Portfolio);
  ASSERT_TRUE(b);
  EXPECT_EQ(b->generation, 2u);
  EXPECT_EQ(b->json, R"({"workbook_id":1,"v":2})");

  EXPECT_FALSE(hub.latest(1, algocraft::StatusSseHub::Channel::Containers).has_value());
  hub.push(1, algocraft::StatusSseHub::Channel::Containers, "[]");
  const auto c = hub.latest(1, algocraft::StatusSseHub::Channel::Containers);
  ASSERT_TRUE(c);
  EXPECT_EQ(c->generation, 1u);
  EXPECT_EQ(c->json, "[]");
}
