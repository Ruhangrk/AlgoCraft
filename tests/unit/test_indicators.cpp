#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/indicators/ema.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/indicators/rsi.hpp"
#include "algocraft/indicators/sma.hpp"
#include "algocraft/indicators/vwap.hpp"

#include <gtest/gtest.h>

namespace {

algocraft::BarEvent close_bar(algocraft::SymbolId id, std::int64_t ns, std::int64_t close_paise,
                              std::int64_t volume = 1000,
                              algocraft::BarResolution res = algocraft::BarResolution::OneMin) {
  algocraft::BarEvent bar{};
  bar.symbol_id = id;
  bar.timestamp = algocraft::Timestamp::from_nanos(ns);
  bar.resolution = res;
  bar.open = algocraft::Price::from_paise(close_paise);
  bar.high = algocraft::Price::from_paise(close_paise);
  bar.low = algocraft::Price::from_paise(close_paise);
  bar.close = algocraft::Price::from_paise(close_paise);
  bar.volume = algocraft::Quantity::from_shares(volume);
  return bar;
}

}  // namespace

TEST(Indicators, EmaKnownSequence) {
  algocraft::Ema ema(3);
  ema.update(close_bar(1, 1, 10000));
  ema.update(close_bar(1, 2, 11000));
  ema.update(close_bar(1, 3, 12000));
  ASSERT_TRUE(ema.ready());
  EXPECT_NEAR(ema.value(), 11250.0, 1.0);
}

TEST(Indicators, LibraryDedupesSameKey) {
  algocraft::IndicatorLibrary lib;
  auto& a = lib.get<algocraft::Ema>(1, algocraft::BarResolution::OneMin, 9);
  auto& b = lib.get<algocraft::Ema>(1, algocraft::BarResolution::OneMin, 9);
  auto& c = lib.get<algocraft::Ema>(1, algocraft::BarResolution::OneMin, 21);
  EXPECT_EQ(&a, &b);
  EXPECT_NE(&a, &c);
}

TEST(Indicators, VwapResetsOnNewDay) {
  algocraft::Vwap vwap;
  constexpr auto day = 86'400'000'000'000LL;
  vwap.update(close_bar(1, day, 10000, 100));
  const auto first = vwap.value();
  vwap.update(close_bar(1, 2 * day, 20000, 100));
  EXPECT_NEAR(vwap.value(), 20000.0, 1.0);
  EXPECT_NE(vwap.value(), first);
}

TEST(Indicators, RsiReadyAfterPeriod) {
  algocraft::Rsi rsi(3);
  rsi.update(close_bar(1, 1, 10000));
  rsi.update(close_bar(1, 2, 10100));
  rsi.update(close_bar(1, 3, 10200));
  EXPECT_FALSE(rsi.ready());
  rsi.update(close_bar(1, 4, 10300));
  EXPECT_TRUE(rsi.ready());
  EXPECT_GT(rsi.value(), 50.0);
}

TEST(Indicators, SmaIgnoresWrongResolution) {
  algocraft::Sma sma(3);
  sma.update(close_bar(1, 1, 10000, 1000, algocraft::BarResolution::OneMin));
  sma.update(close_bar(1, 2, 11000, 1000, algocraft::BarResolution::OneMin));
  EXPECT_FALSE(sma.ready());
  sma.update(close_bar(1, 3, 10000, 1000, algocraft::BarResolution::OneDay));
  sma.update(close_bar(1, 4, 11000, 1000, algocraft::BarResolution::OneDay));
  sma.update(close_bar(1, 5, 12000, 1000, algocraft::BarResolution::OneDay));
  ASSERT_TRUE(sma.ready());
  EXPECT_NEAR(sma.value(), 11000.0, 0.1);
}

TEST(Indicators, SmaRollingWindow) {
  algocraft::Sma sma(2);
  const auto d = algocraft::BarResolution::OneDay;
  sma.update(close_bar(1, 1, 10000, 1, d));
  sma.update(close_bar(1, 2, 20000, 1, d));
  ASSERT_TRUE(sma.ready());
  EXPECT_NEAR(sma.value(), 15000.0, 0.1);
  sma.update(close_bar(1, 3, 30000, 1, d));
  EXPECT_NEAR(sma.value(), 25000.0, 0.1);
}
