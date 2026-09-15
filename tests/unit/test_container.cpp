#include "algocraft/container/trading_container.hpp"
#include "algocraft/execution/simulated_exchange.hpp"
#include "algocraft/portfolio/capital_manager.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

#include <chrono>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

using algocraft::BarEvent;
using algocraft::Capital;
using algocraft::CapitalManager;
using algocraft::ContainerId;
using algocraft::ContainerMode;
using algocraft::ContainerStatus;
using algocraft::Price;
using algocraft::Quantity;
using algocraft::RiskEngine;
using algocraft::Side;
using algocraft::SimulatedExchange;
using algocraft::Strategy;
using algocraft::StrategyConfig;
using algocraft::StrategyId;
using algocraft::StrategyMetadata;
using algocraft::TradingContainer;
using algocraft::TradingContainerConfig;
using algocraft::TradingMode;
using algocraft::UserId;
using algocraft::WorkbookManager;

namespace {

algocraft::Timestamp ist_clock(int hour, int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{2026} / 8 / 18} + hours{hour} + minutes{minute} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

BarEvent bar(int minute, std::int64_t close_paise, algocraft::SymbolId symbol = 1) {
  BarEvent b{};
  b.symbol_id = symbol;
  b.timestamp = ist_clock(10, minute);
  b.open = b.high = b.low = b.close = Price::from_paise(close_paise);
  b.volume = Quantity::from_shares(1'000);
  return b;
}

class OneShotBuy final : public Strategy {
public:
  void configure(const StrategyConfig& config, algocraft::IndicatorLibrary&) override {
    config_ = config;
  }
  void on_bar(const BarEvent&, const algocraft::PortfolioView& portfolio,
              std::vector<algocraft::OrderIntent>& out) override {
    if (bought_ || portfolio.position.shares() > 0) {
      return;
    }
    out.push_back(algocraft::make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy,
                                         config_.order_qty));
    bought_ = true;
  }
  void on_fill(const algocraft::FillEvent&) override {}
  void on_order_update(const algocraft::OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override {
    return {"oneshot", "1.0.0", TradingMode::Mis, algocraft::BarResolution::OneMin, {}};
  }

private:
  StrategyConfig config_{};
  bool bought_{false};
};

TradingContainerConfig base_cfg() {
  TradingContainerConfig cfg{};
  cfg.id = ContainerId::from(1);
  cfg.workbook_id = algocraft::WorkbookId::from_u64(1);
  cfg.symbol_id = 1;
  cfg.strategy_id = StrategyId::from(1);
  cfg.mode = ContainerMode::Backtest;
  cfg.sim_cash = Capital::from_paise(50'000'00);
  cfg.real_allocation = Capital::from_paise(50'000'00);
  cfg.strategy.order_qty = Quantity::from_shares(10);
  return cfg;
}

}  // namespace

TEST(TradingContainer, WarmupStartUpgradeToPaper) {
  SimulatedExchange venue;
  RiskEngine risk;
  auto cfg = base_cfg();
  TradingContainer container(cfg, std::make_unique<OneShotBuy>(), venue, risk);
  container.warmup({bar(0, 10000), bar(1, 10010), bar(2, 10020)});
  EXPECT_EQ(container.status(), ContainerStatus::WarmingUp);
  EXPECT_EQ(container.position().shares(), 0);

  ASSERT_TRUE(container.start().ok);
  EXPECT_EQ(container.status(), ContainerStatus::Active);
  EXPECT_EQ(container.mode(), ContainerMode::Backtest);

  container.on_bar(bar(3, 10030));
  EXPECT_EQ(container.position().shares(), 10);

  ASSERT_TRUE(container.upgrade(ContainerMode::Paper).ok);
  EXPECT_EQ(container.mode(), ContainerMode::Paper);
  EXPECT_EQ(container.position().shares(), 10);

  container.force_exit(Price::from_paise(10040));
  EXPECT_EQ(container.status(), ContainerStatus::Stopped);
  EXPECT_EQ(container.position().shares(), 0);
}

TEST(TradingContainer, CapitalFlowsWorkbookToContainerAndBack) {
  WorkbookManager mgr;
  const auto wb = mgr.create(UserId::from_u64(1), "flow", Capital::from_paise(10'00'000'00));
  const auto borrow = mgr.borrow_capital(wb, Capital::from_paise(1'00'000'00));
  auto* ledger = mgr.activity(borrow.id);
  CapitalManager capital{*ledger};
  SimulatedExchange venue;
  RiskEngine risk;

  auto cfg = base_cfg();
  cfg.workbook_id = wb;
  TradingContainer container(cfg, std::make_unique<OneShotBuy>(), venue, risk, &capital);

  ASSERT_TRUE(container.start().ok);
  EXPECT_EQ(ledger->paper().paise(), 50'000'00);
  EXPECT_EQ(ledger->available().paise(), 1'00'000'00);

  container.on_bar(bar(0, 10000));
  EXPECT_GT(container.position().shares(), 0);

  ASSERT_TRUE(container.upgrade(ContainerMode::Paper).ok);
  ASSERT_TRUE(container.upgrade(ContainerMode::Real).ok);
  EXPECT_EQ(container.mode(), ContainerMode::Real);
  EXPECT_EQ(ledger->paper().paise(), 0);
  EXPECT_EQ(ledger->allocated().paise(), 50'000'00);
  EXPECT_EQ(ledger->available().paise(), 50'000'00);

  container.force_exit(Price::from_paise(10100));
  EXPECT_TRUE(ledger->settled());
  ASSERT_TRUE(mgr.return_capital(wb, borrow.id, ledger->settlement()).ok);
  const auto book = mgr.get_workbook(wb);
  ASSERT_TRUE(book.has_value());
  EXPECT_EQ(book->borrowed_capital.paise(), 0);
  EXPECT_NE(book->available_capital.paise(), 10'00'000'00);
}

TEST(TradingContainer, DuplicateBlockedInSameWorkbook) {
  SimulatedExchange venue;
  RiskEngine risk;
  auto cfg = base_cfg();
  TradingContainer a(cfg, std::make_unique<OneShotBuy>(), venue, risk);
  auto cfg2 = cfg;
  cfg2.id = ContainerId::from(2);
  TradingContainer b(cfg2, std::make_unique<OneShotBuy>(), venue, risk);
  ASSERT_TRUE(a.start().ok);
  EXPECT_FALSE(b.start().ok);
  a.stop();
  EXPECT_TRUE(b.start().ok);
  b.stop();
}

TEST(TradingContainer, KillSwitchForceExits) {
  SimulatedExchange venue;
  RiskEngine risk;
  auto cfg = base_cfg();
  TradingContainer container(cfg, std::make_unique<OneShotBuy>(), venue, risk);
  ASSERT_TRUE(container.start().ok);
  container.on_bar(bar(0, 10000));
  EXPECT_GT(container.position().shares(), 0);

  algocraft::SystemEvent ev{};
  ev.type = algocraft::SystemEventType::KillSwitch;
  container.on_system_event(ev);
  EXPECT_EQ(container.status(), ContainerStatus::Stopped);
  EXPECT_EQ(container.position().shares(), 0);
  EXPECT_TRUE(risk.kill_switch());
}

TEST(TradingContainer, RiskBlocksOversizedBuy) {
  SimulatedExchange venue;
  algocraft::RiskLimits limits{};
  limits.max_position = Quantity::from_shares(5);
  RiskEngine risk{limits};
  auto cfg = base_cfg();
  cfg.strategy.order_qty = Quantity::from_shares(10);
  TradingContainer container(cfg, std::make_unique<OneShotBuy>(), venue, risk);
  ASSERT_TRUE(container.start().ok);
  container.on_bar(bar(0, 10000));
  EXPECT_EQ(container.position().shares(), 0);
  EXPECT_STREQ(container.last_rejection().rule, "MaxPositionSize");
}
