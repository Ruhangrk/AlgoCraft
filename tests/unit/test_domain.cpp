#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/timestamp.hpp"

#include <gtest/gtest.h>

TEST(Domain, PriceArithmetic) {
  const auto a = algocraft::Price::from_paise(250);
  const auto b = algocraft::Price::from_paise(50);
  EXPECT_EQ((a + b).paise(), 300);
  EXPECT_EQ((a - b).paise(), 200);
  EXPECT_GT(a, b);
}

TEST(Domain, QuantityAndTimestamp) {
  EXPECT_EQ(algocraft::Quantity::from_shares(100).shares(), 100);
  const auto ts = algocraft::Timestamp::from_nanos(123);
  EXPECT_EQ(ts.nanos(), 123);
  EXPECT_GT(algocraft::Timestamp::now().nanos(), 0);
}

TEST(Domain, BarEventDefaultsToOneMin) {
  algocraft::BarEvent bar{};
  EXPECT_EQ(bar.resolution, algocraft::BarResolution::OneMin);
  EXPECT_EQ(bar.symbol_id, 0u);
}
