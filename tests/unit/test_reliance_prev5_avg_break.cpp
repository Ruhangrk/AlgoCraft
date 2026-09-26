#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/indicators/lagged_sma.hpp"
#include "algocraft/strategies/reliance_prev5_avg_break.hpp"

#include <gtest/gtest.h>

namespace {

algocraft::BarEvent close_at(int minute, std::int64_t close_paise) {
  constexpr std::int64_t kOpenUnix = 1787024700LL;  // 2026-08-18 09:15 IST
  algocraft::BarEvent bar{};
  bar.symbol_id = 1;
  bar.timestamp =
      algocraft::Timestamp::from_nanos((kOpenUnix + minute * 60) * 1'000'000'000LL);
  bar.open = bar.high = bar.low = bar.close = algocraft::Price::from_paise(close_paise);
  bar.volume = algocraft::Quantity::from_shares(1000);
  bar.resolution = algocraft::BarResolution::OneMin;
  return bar;
}

struct Fixture {
  algocraft::IndicatorLibrary lib;
  algocraft::ReliancePrev5AvgBreak strat;
  algocraft::LaggedSma* avg{nullptr};

  Fixture() {
    algocraft::StrategyConfig cfg{};
    cfg.symbol_id = 1;
    strat.configure(cfg, lib);
    avg = &lib.get<algocraft::LaggedSma>(1, algocraft::BarResolution::OneMin, 5);
  }

  void feed(int minute, std::int64_t close_paise) { avg->update(close_at(minute, close_paise)); }
};

}  // namespace

TEST(LaggedSma, ReadyAfterSixBarsWithPriorFive) {
  algocraft::LaggedSma sma(5);
  for (int i = 0; i < 5; ++i) {
    sma.update(close_at(i, 100000));
    EXPECT_FALSE(sma.ready());
  }
  sma.update(close_at(5, 101000));
  ASSERT_TRUE(sma.ready());
  EXPECT_NEAR(sma.value(), 100000.0, 0.1);
}

TEST(ReliancePrev5AvgBreak, BuysWhen030PctAbovePriorAvg) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 100400);  // +0.40%

  algocraft::PortfolioView view{};
  view.cash = algocraft::Capital::from_paise(10'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 100400), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
}

TEST(ReliancePrev5AvgBreak, NoBuyBelowNewThreshold) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 100200);  // +0.20% < 0.30%

  algocraft::PortfolioView view{};
  view.cash = algocraft::Capital::from_paise(10'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 100200), view, out);
  EXPECT_TRUE(out.empty());
}

TEST(ReliancePrev5AvgBreak, NoStackWhileLong) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 100400);

  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(10);
  view.cash = algocraft::Capital::from_paise(5'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 100400), view, out);
  EXPECT_TRUE(out.empty());
}

TEST(ReliancePrev5AvgBreak, SellsOnStopBelowAvg) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 99500);  // −0.50%

  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(25);
  view.cash = algocraft::Capital::from_paise(1'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 99500), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);
}

TEST(ReliancePrev5AvgBreak, SellsOnTakeProfitAboveAvg) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 100600);  // +0.60%

  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(10);
  view.cash = algocraft::Capital::from_paise(1'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 100600), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);
}
