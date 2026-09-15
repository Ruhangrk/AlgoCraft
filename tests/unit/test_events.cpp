#include "algocraft/domain/events.hpp"

#include <gtest/gtest.h>

TEST(Events, OrderIntentAndFillEvent) {
  algocraft::OrderIntent intent{};
  intent.strategy_id = algocraft::StrategyId::from(3);
  intent.symbol_id = 1;
  intent.side = algocraft::Side::Sell;
  intent.quantity = algocraft::Quantity::from_shares(25);
  intent.type = algocraft::OrderType::Limit;
  intent.price = algocraft::Price::from_paise(10000);

  algocraft::FillEvent fill{};
  fill.order_id = algocraft::OrderId::from(9);
  fill.symbol_id = intent.symbol_id;
  fill.side = intent.side;
  fill.filled_qty = intent.quantity;
  fill.fill_price = intent.price;
  fill.workbook_id = algocraft::Uuid::from_u64(2);

  EXPECT_EQ(fill.symbol_id, intent.symbol_id);
  EXPECT_EQ(fill.side, algocraft::Side::Sell);
}

TEST(Events, SystemAndWorkbook) {
  algocraft::SystemEvent sys{};
  sys.type = algocraft::SystemEventType::MisSquareoffWarning;

  algocraft::WorkbookEvent wb{};
  wb.type = algocraft::WorkbookEventType::ActivityBorrow;
  wb.amount = algocraft::Capital::from_paise(50'000'00);

  EXPECT_EQ(sys.type, algocraft::SystemEventType::MisSquareoffWarning);
  EXPECT_EQ(wb.type, algocraft::WorkbookEventType::ActivityBorrow);
}

TEST(Events, OrderUpdateStatus) {
  algocraft::OrderUpdate update{};
  update.order_id = algocraft::OrderId::from(1);
  update.status = algocraft::OrderStatus::Rejected;
  EXPECT_EQ(update.status, algocraft::OrderStatus::Rejected);
}
