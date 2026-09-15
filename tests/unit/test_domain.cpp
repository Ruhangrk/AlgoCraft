#include "algocraft/domain/account.hpp"
#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/fill.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/order.hpp"
#include "algocraft/domain/position.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/domain/user.hpp"
#include "algocraft/domain/workbook.hpp"

#include <type_traits>

#include <gtest/gtest.h>

TEST(Domain, PriceArithmetic) {
  const auto a = algocraft::Price::from_paise(250);
  const auto b = algocraft::Price::from_paise(50);
  EXPECT_EQ((a + b).paise(), 300);
  EXPECT_EQ((a - b).paise(), 200);
  EXPECT_GT(a, b);
}

TEST(Domain, RoundToTick) {
  const auto tick = algocraft::Price::from_paise(5);
  EXPECT_EQ(algocraft::round_to_tick(algocraft::Price::from_paise(12346), tick).paise(), 12345);
  EXPECT_EQ(algocraft::round_to_tick(algocraft::Price::from_paise(12348), tick).paise(), 12350);
}

TEST(Domain, QuantityLotRounding) {
  const auto lot = algocraft::Quantity::from_shares(25);
  EXPECT_EQ(algocraft::round_down_to_lot(algocraft::Quantity::from_shares(80), lot).shares(), 75);
  EXPECT_EQ(algocraft::round_down_to_lot(algocraft::Quantity::from_shares(25), lot).shares(), 25);
  EXPECT_EQ(algocraft::round_down_to_lot(algocraft::Quantity::from_shares(10), lot).shares(), 0);
  EXPECT_EQ(algocraft::round_down_to_lot(algocraft::Quantity::from_shares(10),
                                         algocraft::Quantity::from_shares(0))
                .shares(),
            0);
}

TEST(Domain, NotionalAndCapital) {
  const auto n = algocraft::notional(algocraft::Price::from_paise(10050),
                                    algocraft::Quantity::from_shares(10));
  EXPECT_EQ(n.paise(), 100500);
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

TEST(Domain, IdsDoNotMixByType) {
  const auto order = algocraft::OrderId::from(1);
  const auto fill = algocraft::FillId::from(1);
  EXPECT_EQ(order.value, fill.value);
  static_assert(!std::is_same_v<algocraft::OrderId, algocraft::FillId>);
}

TEST(Domain, UuidFromU64) {
  const auto a = algocraft::Uuid::from_u64(7);
  const auto b = algocraft::Uuid::from_u64(7);
  const auto c = algocraft::Uuid::from_u64(8);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

TEST(Domain, WorkbookAndUser) {
  algocraft::User user{};
  user.id = algocraft::Uuid::from_u64(1);
  user.username = "ruhang";
  user.role = algocraft::Role::Admin;

  algocraft::Workbook book{};
  book.id = algocraft::Uuid::from_u64(2);
  book.user_id = user.id;
  book.name = "NSE Test";
  book.main_capital = algocraft::Capital::from_paise(10'00'000'00);
  book.status = algocraft::WorkbookStatus::Active;

  EXPECT_EQ(book.user_id, user.id);
  EXPECT_EQ(user.role, algocraft::Role::Admin);
}

TEST(Domain, PositionAccountOrderFill) {
  algocraft::Position pos{};
  pos.symbol_id = 1;
  pos.net_qty = algocraft::Quantity::from_shares(50);
  pos.average_price = algocraft::Price::from_paise(10000);

  algocraft::Account account{};
  account.capital = algocraft::Capital::from_paise(1'00'000'00);
  account.mode = algocraft::ContainerMode::Paper;

  algocraft::Order order{};
  order.order_id = algocraft::OrderId::from(9);
  order.symbol_id = 1;
  order.side = algocraft::Side::Buy;
  order.type = algocraft::OrderType::Market;
  order.trading_mode = algocraft::TradingMode::Mis;
  order.quantity = algocraft::Quantity::from_shares(25);
  order.workbook_id = algocraft::Uuid::from_u64(2);

  algocraft::Fill fill{};
  fill.order_id = order.order_id;
  fill.filled_qty = order.quantity;
  fill.fill_price = algocraft::Price::from_paise(10000);

  EXPECT_EQ(fill.order_id, order.order_id);
  EXPECT_EQ(account.mode, algocraft::ContainerMode::Paper);
  EXPECT_EQ(pos.net_qty.shares(), 50);
}

TEST(Domain, HotPathTypeSizes) {
  EXPECT_LE(sizeof(algocraft::Order), 128u);
  EXPECT_LE(sizeof(algocraft::Fill), 96u);
  EXPECT_LE(sizeof(algocraft::BarEvent), 64u);
  EXPECT_LE(sizeof(algocraft::OrderIntent), 64u);
}
