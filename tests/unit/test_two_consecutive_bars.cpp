#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/execution/cost_calculator.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/indicators/sma.hpp"
#include "algocraft/strategies/two_consecutive_bars.hpp"

#include <gtest/gtest.h>

namespace {

constexpr std::int64_t kOpenUnix = 1787024700LL;  // 2026-08-18 09:15 IST

algocraft::BarEvent ohlc(int minutes_after_open, std::int64_t o, std::int64_t h, std::int64_t l,
                         std::int64_t c) {
  algocraft::BarEvent bar{};
  bar.symbol_id = 1;
  bar.timestamp =
      algocraft::Timestamp::from_nanos((kOpenUnix + minutes_after_open * 60) * 1'000'000'000LL);
  bar.open = algocraft::Price::from_paise(o);
  bar.high = algocraft::Price::from_paise(h);
  bar.low = algocraft::Price::from_paise(l);
  bar.close = algocraft::Price::from_paise(c);
  bar.volume = algocraft::Quantity::from_shares(1000);
  bar.resolution = algocraft::BarResolution::OneMin;
  return bar;
}

algocraft::BarEvent daily(int day_index, std::int64_t close_paise) {
  algocraft::BarEvent bar{};
  bar.symbol_id = 1;
  // Prior days before kOpenUnix session.
  const auto day_ns = 86'400'000'000'000LL;
  bar.timestamp = algocraft::Timestamp::from_nanos(kOpenUnix * 1'000'000'000LL - day_ns * (10 - day_index));
  bar.open = bar.high = bar.low = bar.close = algocraft::Price::from_paise(close_paise);
  bar.volume = algocraft::Quantity::from_shares(1'000'000);
  bar.resolution = algocraft::BarResolution::OneDay;
  return bar;
}

struct Fixture {
  algocraft::IndicatorLibrary lib;
  algocraft::TwoConsecutiveBars strat;

  explicit Fixture(std::int64_t alloc_paise = 10'00'000'00, int sma_period = 3,
                   std::int64_t daily_close = 90000) {
    algocraft::StrategyConfig cfg{};
    cfg.symbol_id = 1;
    cfg.alloc_paise = alloc_paise;
    cfg.sma_period = sma_period;
    strat.configure(cfg, lib);
    auto& sma = lib.get<algocraft::Sma>(1, algocraft::BarResolution::OneDay, sma_period);
    for (int i = 0; i < sma_period; ++i) {
      sma.update(daily(i, daily_close));
    }
  }
};

}  // namespace

TEST(TwoConsecutiveBars, BuysOnThreeGreen) {
  Fixture f;
  algocraft::PortfolioView flat{};
  std::vector<algocraft::OrderIntent> out;

  // Isolate 3-green path with tiny range (< 0.25%).
  f.strat.on_bar(ohlc(0, 100000, 100050, 100000, 100040), flat, out);
  EXPECT_TRUE(out.empty());
  out.clear();
  f.strat.on_bar(ohlc(1, 100040, 100100, 100030, 100090), flat, out);
  EXPECT_TRUE(out.empty());
  out.clear();
  f.strat.on_bar(ohlc(2, 100090, 100150, 100080, 100140), flat, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
  EXPECT_GT(out[0].quantity.shares(), 0);
}

TEST(TwoConsecutiveBars, BuysOnTwoGreenRange) {
  Fixture f;
  algocraft::PortfolioView flat{};
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(ohlc(0, 100000, 100100, 100000, 100080), flat, out);
  EXPECT_TRUE(out.empty());
  out.clear();
  f.strat.on_bar(ohlc(1, 100080, 100300, 100050, 100250), flat, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
}

TEST(TwoConsecutiveBars, NoBuyBelowSma) {
  Fixture f(10'00'000'00, 3, /*daily_close=*/150000);  // SMA = 1500 > 1m closes
  algocraft::PortfolioView flat{};
  std::vector<algocraft::OrderIntent> out;
  f.strat.on_bar(ohlc(0, 100000, 100100, 100000, 100080), flat, out);
  out.clear();
  f.strat.on_bar(ohlc(1, 100080, 100300, 100050, 100250), flat, out);
  EXPECT_TRUE(out.empty());
}

TEST(TwoConsecutiveBars, NoBuyInLastHour) {
  Fixture f;
  algocraft::PortfolioView flat{};
  std::vector<algocraft::OrderIntent> out;
  const int start = 315;  // 14:30
  f.strat.on_bar(ohlc(start, 100000, 100100, 100000, 100080), flat, out);
  out.clear();
  f.strat.on_bar(ohlc(start + 1, 100080, 100300, 100050, 100250), flat, out);
  EXPECT_TRUE(out.empty());
}

TEST(TwoConsecutiveBars, SellsOnThreeRed) {
  Fixture f;
  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(10);
  std::vector<algocraft::OrderIntent> out;

  algocraft::FillEvent buy{};
  buy.side = algocraft::Side::Buy;
  buy.fill_price = algocraft::Price::from_paise(100000);
  buy.filled_qty = algocraft::Quantity::from_shares(10);
  buy.fees = algocraft::Capital::from_paise(2500);
  f.strat.on_fill(buy);

  f.strat.on_bar(ohlc(0, 100200, 100200, 100050, 100050), view, out);
  out.clear();
  f.strat.on_bar(ohlc(1, 100050, 100050, 99900, 99900), view, out);
  out.clear();
  f.strat.on_bar(ohlc(2, 99900, 99900, 99700, 99700), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);
  EXPECT_EQ(out[0].quantity.shares(), 10);
}

TEST(TwoConsecutiveBars, SellWinsOverBuySameBar) {
  Fixture f;
  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(5);
  std::vector<algocraft::OrderIntent> out;

  algocraft::FillEvent buy{};
  buy.side = algocraft::Side::Buy;
  buy.fill_price = algocraft::Price::from_paise(100000);
  buy.filled_qty = algocraft::Quantity::from_shares(5);
  buy.fees = algocraft::Capital::from_paise(2500);
  f.strat.on_fill(buy);

  f.strat.on_bar(ohlc(0, 100100, 100100, 100000, 100000), view, out);
  out.clear();
  f.strat.on_bar(ohlc(1, 100000, 100000, 99900, 99900), view, out);
  out.clear();
  f.strat.on_bar(ohlc(2, 99900, 99900, 99800, 99800), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);
}

TEST(TwoConsecutiveBars, SellsNearBreakEvenAfterFees) {
  Fixture f;
  algocraft::PortfolioView view{};
  const auto qty = algocraft::Quantity::from_shares(10);
  view.position = qty;
  std::vector<algocraft::OrderIntent> out;

  const auto entry = algocraft::Price::from_paise(100000);
  algocraft::CostCalculator costs;
  const auto buy_fee = costs.buy_fees(entry, qty);
  algocraft::FillEvent buy{};
  buy.side = algocraft::Side::Buy;
  buy.fill_price = entry;
  buy.filled_qty = qty;
  buy.fees = buy_fee;
  f.strat.on_fill(buy);

  const auto cost = algocraft::notional(entry, qty).paise() + buy_fee.paise();
  std::int64_t be_px = entry.paise();
  for (std::int64_t p = entry.paise(); p < entry.paise() + 5000; ++p) {
    const auto sp = algocraft::Price::from_paise(p);
    const auto fee = costs.sell_fees(sp, qty).paise();
    const auto proceeds = algocraft::notional(sp, qty).paise() - fee;
    const auto tol = std::max<std::int64_t>(100, cost * 5 / 10000);
    if (proceeds >= cost - tol && proceeds <= cost + tol) {
      be_px = p;
      break;
    }
  }

  out.clear();
  f.strat.on_bar(ohlc(0, be_px - 30, be_px + 5, be_px - 40, be_px), view, out);
  for (const auto& intent : out) {
    EXPECT_NE(intent.side, algocraft::Side::Sell) << "must not BE-sell on green";
  }
  out.clear();
  f.strat.on_bar(ohlc(1, be_px + 40, be_px + 40, be_px - 5, be_px), view, out);
  for (const auto& intent : out) {
    EXPECT_NE(intent.side, algocraft::Side::Sell) << "must not BE-sell on first red only";
  }
  out.clear();
  f.strat.on_bar(ohlc(2, be_px + 30, be_px + 30, be_px - 5, be_px), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Sell);
}

TEST(TwoConsecutiveBars, BuyAgainWhileLong) {
  Fixture f;
  algocraft::PortfolioView view{};
  view.position = algocraft::Quantity::from_shares(10);
  std::vector<algocraft::OrderIntent> out;

  algocraft::FillEvent buy{};
  buy.side = algocraft::Side::Buy;
  buy.fill_price = algocraft::Price::from_paise(90000);
  buy.filled_qty = algocraft::Quantity::from_shares(10);
  buy.fees = algocraft::Capital::from_paise(2500);
  f.strat.on_fill(buy);

  f.strat.on_bar(ohlc(0, 100000, 100100, 100000, 100080), view, out);
  out.clear();
  f.strat.on_bar(ohlc(1, 100080, 100300, 100050, 100250), view, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].side, algocraft::Side::Buy);
}
