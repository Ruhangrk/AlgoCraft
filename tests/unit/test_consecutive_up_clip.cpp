#include "algocraft/domain/bar_event.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/strategies/consecutive_up_clip.hpp"

#include <gtest/gtest.h>

namespace {

constexpr std::int64_t kOpenUnix = 1787024700LL;  // 2026-08-18 09:15 IST

algocraft::BarEvent bar_at(int minutes_after_open, std::int64_t close_paise) {
  algocraft::BarEvent bar{};
  bar.symbol_id = 1;
  bar.timestamp =
      algocraft::Timestamp::from_nanos((kOpenUnix + minutes_after_open * 60) * 1'000'000'000LL);
  bar.open = algocraft::Price::from_paise(close_paise);
  bar.high = algocraft::Price::from_paise(close_paise);
  bar.low = algocraft::Price::from_paise(close_paise);
  bar.close = algocraft::Price::from_paise(close_paise);
  bar.volume = algocraft::Quantity::from_shares(1000);
  bar.resolution = algocraft::BarResolution::OneMin;
  return bar;
}

algocraft::ConsecutiveUpClip make_strat() {
  algocraft::ConsecutiveUpClip s;
  algocraft::IndicatorLibrary lib;
  algocraft::StrategyConfig cfg{};
  cfg.symbol_id = 1;
  cfg.entry_up_bars = 8;
  cfg.add_up_bars = 3;
  cfg.take_profit_bps = 50;
  cfg.stop_bps = 30;
  cfg.add_max_dip_bps = 25;
  cfg.clip_paise = 20'00'000'00;
  s.configure(cfg, lib);
  return s;
}

}  // namespace

TEST(ConsecutiveUpClip, BuysAfterEightUpCloses) {
  auto s = make_strat();
  algocraft::PortfolioView flat{};
  std::vector<algocraft::OrderIntent> out;
  std::int64_t px = 100000;
  for (int i = 0; i < 8; ++i) {
    out.clear();
    s.on_bar(bar_at(i, px), flat, out);
    EXPECT_TRUE(out.empty()) << i;
    px += 100;
  }
  out.clear();
  s.on_bar(bar_at(8, px), flat, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
  EXPECT_EQ(out[0].quantity.shares(), 20'00'000'00 / px);
}

TEST(ConsecutiveUpClip, NoBuyInLastHour) {
  auto s = make_strat();
  algocraft::PortfolioView flat{};
  std::vector<algocraft::OrderIntent> out;
  std::int64_t px = 100000;
  const int start = 315;  // 14:30
  for (int i = 0; i < 9; ++i) {
    out.clear();
    s.on_bar(bar_at(start + i, px), flat, out);
    EXPECT_TRUE(out.empty()) << i;
    px += 100;
  }
}

TEST(ConsecutiveUpClip, AddsAfterDipThenThreeUp) {
  auto s = make_strat();
  algocraft::PortfolioView view{};
  std::vector<algocraft::OrderIntent> out;
  std::int64_t px = 100000;
  for (int i = 0; i <= 8; ++i) {
    out.clear();
    s.on_bar(bar_at(i, px), view, out);
    if (i < 8) {
      px += 50;
    }
  }
  ASSERT_EQ(out.size(), 1u);
  algocraft::FillEvent fill{};
  fill.side = algocraft::Side::Buy;
  fill.fill_price = algocraft::Price::from_paise(px);
  fill.filled_qty = out[0].quantity;
  s.on_fill(fill);
  view.position = fill.filled_qty;

  out.clear();
  s.on_bar(bar_at(9, px - 200), view, out);  // 0.20% down from 100400-ish
  EXPECT_TRUE(out.empty());
  s.on_bar(bar_at(10, px - 150), view, out);
  EXPECT_TRUE(out.empty());
  s.on_bar(bar_at(11, px - 100), view, out);
  EXPECT_TRUE(out.empty());
  out.clear();
  s.on_bar(bar_at(12, px - 50), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
}

TEST(ConsecutiveUpClip, TakeProfitVsFirstFillThenResets) {
  auto s = make_strat();
  algocraft::PortfolioView view{};
  std::vector<algocraft::OrderIntent> out;
  std::int64_t px = 100000;
  for (int i = 0; i <= 8; ++i) {
    out.clear();
    s.on_bar(bar_at(i, px), view, out);
    if (i < 8) {
      px += 10;
    }
  }
  ASSERT_FALSE(out.empty());
  const auto p0 = px;
  algocraft::FillEvent fill{};
  fill.side = algocraft::Side::Buy;
  fill.fill_price = algocraft::Price::from_paise(p0);
  fill.filled_qty = out[0].quantity;
  s.on_fill(fill);
  view.position = fill.filled_qty;

  out.clear();
  s.on_bar(bar_at(9, p0 + (p0 * 50) / 10000 + 10), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);

  algocraft::FillEvent sell = fill;
  sell.side = algocraft::Side::Sell;
  s.on_fill(sell);
  view.position = {};

  out.clear();
  px = p0;
  for (int i = 0; i <= 8; ++i) {
    out.clear();
    s.on_bar(bar_at(20 + i, px), view, out);
    px += 20;
  }
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
}
