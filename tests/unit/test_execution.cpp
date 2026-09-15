#include "algocraft/execution/cost_calculator.hpp"
#include "algocraft/execution/simulated_exchange.hpp"

#include <gtest/gtest.h>

TEST(CostCalculator, BuyAndSellFeesPositive) {
  algocraft::CostCalculator calc;
  const auto px = algocraft::Price::from_paise(100000);
  const auto qty = algocraft::Quantity::from_shares(10);
  const auto buy = calc.buy_fees(px, qty);
  const auto sell = calc.sell_fees(px, qty);
  EXPECT_GT(buy.paise(), 2000);
  EXPECT_GT(sell.paise(), buy.paise());
}

TEST(SimulatedExchange, BuyThenSell) {
  algocraft::SimulatedExchange sim;
  algocraft::BarEvent bar{};
  bar.close = algocraft::Price::from_paise(10000);
  bar.timestamp = algocraft::Timestamp::from_nanos(1);

  algocraft::OrderIntent buy{};
  buy.symbol_id = 1;
  buy.side = algocraft::Side::Buy;
  buy.quantity = algocraft::Quantity::from_shares(1);
  buy.type = algocraft::OrderType::Market;

  const auto cash = algocraft::Capital::from_paise(1'00'000'00);
  const auto fill_buy =
      sim.submit(buy, bar, algocraft::TradingMode::Mis, cash, algocraft::Quantity{});
  ASSERT_TRUE(fill_buy);
  EXPECT_EQ(fill_buy->side, algocraft::Side::Buy);
  EXPECT_EQ(fill_buy->fill_price.paise(), 10001);
  EXPECT_LT(fill_buy->net_cash_impact.paise(), 0);

  algocraft::OrderIntent sell = buy;
  sell.side = algocraft::Side::Sell;
  const auto fill_sell = sim.submit(sell, bar, algocraft::TradingMode::Mis, cash,
                                    fill_buy->filled_qty, fill_buy->fill_price);
  ASSERT_TRUE(fill_sell);
  EXPECT_EQ(fill_sell->fill_price.paise(), 9999);
}

TEST(SimulatedExchange, AllowsPyramidBuy) {
  algocraft::SimulatedExchange sim;
  algocraft::BarEvent bar{};
  bar.close = algocraft::Price::from_paise(10000);
  algocraft::OrderIntent buy{};
  buy.symbol_id = 1;
  buy.side = algocraft::Side::Buy;
  buy.quantity = algocraft::Quantity::from_shares(1);
  const auto cash = algocraft::Capital::from_paise(1'00'000'00);
  const auto a = sim.submit(buy, bar, algocraft::TradingMode::Mis, cash, algocraft::Quantity{});
  ASSERT_TRUE(a);
  const auto b =
      sim.submit(buy, bar, algocraft::TradingMode::Mis, cash, a->filled_qty, a->fill_price);
  ASSERT_TRUE(b);
}

TEST(SimulatedExchange, RejectsSellWhenFlat) {
  algocraft::SimulatedExchange sim;
  algocraft::BarEvent bar{};
  bar.close = algocraft::Price::from_paise(10000);
  algocraft::OrderIntent sell{};
  sell.side = algocraft::Side::Sell;
  sell.quantity = algocraft::Quantity::from_shares(1);
  EXPECT_FALSE(sim.submit(sell, bar, algocraft::TradingMode::Mis,
                          algocraft::Capital::from_paise(1'00'000'00), algocraft::Quantity{}));
}
