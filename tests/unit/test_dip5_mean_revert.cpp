#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/indicators/lagged_sma.hpp"
#include "algocraft/strategies/dip5_mean_revert.hpp"

#include <gtest/gtest.h>

namespace {

algocraft::BarEvent close_at(int minute, std::int64_t close_paise) {
  constexpr std::int64_t kOpenUnix = 1787024700LL;
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
  algocraft::Dip5MeanRevert strat;
  algocraft::LaggedSma* avg{nullptr};

  Fixture() {
    algocraft::StrategyConfig cfg{};
    cfg.symbol_id = 1;
    strat.configure(cfg, lib);
    avg = &lib.get<algocraft::LaggedSma>(1, algocraft::BarResolution::OneMin, 5);
  }

  void feed(int minute, std::int64_t px) { avg->update(close_at(minute, px)); }
};

}  // namespace

TEST(Dip5MeanRevert, BuysOnDipBelowAvg) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 99700);  // −0.30%

  algocraft::PortfolioView view{};
  view.cash = algocraft::Capital::from_paise(10'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 99700), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
}

TEST(Dip5MeanRevert, SellsWhenBackToAvg) {
  Fixture f;
  for (int i = 0; i < 5; ++i) {
    f.feed(i, 100000);
  }
  f.feed(5, 100000);  // at avg

  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(10);
  view.cash = algocraft::Capital::from_paise(1'00'000'00);
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(close_at(5, 100000), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);
}
