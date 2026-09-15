#include "algocraft/domain/session_clock.hpp"
#include "algocraft/risk/risk_engine.hpp"
#include "algocraft/strategies/make_intent.hpp"

#include <chrono>
#include <gtest/gtest.h>

using algocraft::ContainerContext;
using algocraft::ContainerId;
using algocraft::Quantity;
using algocraft::RiskEngine;
using algocraft::RiskLimits;
using algocraft::Side;
using algocraft::StrategyId;
using algocraft::SymbolId;
using algocraft::TradingMode;
using algocraft::WorkbookId;

namespace {

algocraft::Timestamp ist_clock(int hour, int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{2026} / 8 / 18} + hours{hour} + minutes{minute} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

ContainerContext open_ctx() {
  ContainerContext ctx{};
  ctx.workbook_id = WorkbookId::from_u64(1);
  ctx.container_id = ContainerId::from(1);
  ctx.strategy_id = StrategyId::from(1);
  ctx.symbol_id = 1;
  ctx.cash = algocraft::Capital::from_paise(10'00'000'00);
  ctx.now = ist_clock(10, 0);
  return ctx;
}

algocraft::OrderIntent buy(std::int64_t shares, std::int64_t px) {
  return algocraft::make_intent(StrategyId::from(1), 1, Side::Buy,
                                Quantity::from_shares(shares),
                                algocraft::Price::from_paise(px));
}

}  // namespace

TEST(SessionClock, NseRegularHours) {
  EXPECT_TRUE(algocraft::nse_regular_hours(ist_clock(9, 15)));
  EXPECT_TRUE(algocraft::nse_regular_hours(ist_clock(15, 29)));
  EXPECT_FALSE(algocraft::nse_regular_hours(ist_clock(9, 14)));
  EXPECT_FALSE(algocraft::nse_regular_hours(ist_clock(15, 30)));
  EXPECT_FALSE(algocraft::nse_regular_hours(ist_clock(8, 0)));
}

TEST(RiskEngine, ApprovesInHoursBuy) {
  RiskEngine engine;
  const auto result = engine.check(buy(10, 10000), open_ctx());
  EXPECT_TRUE(result.ok());
}

TEST(RiskEngine, MarketHoursRejects) {
  RiskEngine engine;
  auto ctx = open_ctx();
  ctx.now = ist_clock(8, 0);
  const auto result = engine.check(buy(10, 10000), ctx);
  EXPECT_FALSE(result.ok());
  EXPECT_STREQ(result.rule, "MarketHours");
}

TEST(RiskEngine, KillSwitchRejectsEverything) {
  RiskEngine engine;
  engine.set_kill_switch(true);
  const auto result = engine.check(buy(1, 10000), open_ctx());
  EXPECT_FALSE(result.ok());
  EXPECT_STREQ(result.rule, "KillSwitch");
}

TEST(RiskEngine, DailyLossLimit) {
  RiskLimits limits{};
  limits.daily_loss_limit = algocraft::Capital::from_paise(1'000'00);
  RiskEngine engine{limits};
  auto ctx = open_ctx();
  ctx.daily_pnl = algocraft::Capital::from_paise(-1'000'00);
  const auto result = engine.check(buy(1, 10000), ctx);
  EXPECT_FALSE(result.ok());
  EXPECT_STREQ(result.rule, "DailyLossLimit");
}

TEST(RiskEngine, MaxPositionSize) {
  RiskLimits limits{};
  limits.max_position = Quantity::from_shares(10);
  RiskEngine engine{limits};
  auto ctx = open_ctx();
  ctx.position = Quantity::from_shares(8);
  EXPECT_TRUE(engine.check(buy(2, 10000), ctx).ok());
  EXPECT_FALSE(engine.check(buy(3, 10000), ctx).ok());
}

TEST(RiskEngine, ContainerCapital) {
  RiskEngine engine;
  auto ctx = open_ctx();
  ctx.cash = algocraft::Capital::from_paise(100);
  const auto result = engine.check(buy(100, 10'000'00), ctx);
  EXPECT_FALSE(result.ok());
  EXPECT_STREQ(result.rule, "ContainerCapital");
}

TEST(RiskEngine, DuplicateContainerPerWorkbook) {
  RiskEngine engine;
  auto a = open_ctx();
  auto b = open_ctx();
  b.container_id = ContainerId::from(2);
  ASSERT_TRUE(engine.register_container(a).ok);
  EXPECT_FALSE(engine.register_container(b).ok);

  auto other_book = b;
  other_book.workbook_id = WorkbookId::from_u64(2);
  EXPECT_TRUE(engine.register_container(other_book).ok);

  auto other_strategy = b;
  other_strategy.strategy_id = StrategyId::from(9);
  EXPECT_TRUE(engine.register_container(other_strategy).ok);

  engine.unregister_container(a.container_id);
  EXPECT_TRUE(engine.register_container(b).ok);
}

TEST(RiskEngine, MisSquareOffRejectsMis) {
  RiskEngine engine;
  engine.set_mis_squareoff(true);
  auto ctx = open_ctx();
  ctx.trading_mode = TradingMode::Mis;
  EXPECT_FALSE(engine.check(buy(1, 10000), ctx).ok());
  ctx.trading_mode = algocraft::TradingMode::Cnc;
  EXPECT_TRUE(engine.check(buy(1, 10000), ctx).ok());
}

TEST(RiskEngine, SystemEventArmsKillSwitch) {
  RiskEngine engine;
  algocraft::SystemEvent ev{};
  ev.type = algocraft::SystemEventType::KillSwitch;
  engine.on_system_event(ev);
  EXPECT_TRUE(engine.kill_switch());
  EXPECT_FALSE(engine.check(buy(1, 10000), open_ctx()).ok());
}
