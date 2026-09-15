#include "algocraft/portfolio/capital_manager.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

#include <gtest/gtest.h>

using algocraft::Capital;
using algocraft::CapitalManager;
using algocraft::ContainerId;
using algocraft::ContainerMode;
using algocraft::UserId;
using algocraft::WorkbookManager;

TEST(PortfolioLedger, RealAllocateAndReleaseWithPnl) {
  WorkbookManager mgr;
  const auto wb = mgr.create(UserId::from_u64(1), "A", Capital::from_paise(10'00'000'00));
  const auto borrow = mgr.borrow_capital(wb, Capital::from_paise(1'00'000'00));
  auto* ledger = mgr.activity(borrow.id);
  ASSERT_NE(ledger, nullptr);
  EXPECT_EQ(ledger->available().paise(), 1'00'000'00);
  EXPECT_EQ(ledger->allocated().paise(), 0);

  CapitalManager capital{*ledger};
  const auto id = ContainerId::from(7);
  ASSERT_TRUE(capital.allocate(id, Capital::from_paise(40'000'00), ContainerMode::Real).ok);
  EXPECT_EQ(ledger->available().paise(), 60'000'00);
  EXPECT_EQ(ledger->allocated().paise(), 40'000'00);
  EXPECT_EQ(ledger->paper().paise(), 0);

  ASSERT_TRUE(capital.release(id, Capital::from_paise(42'000'00)).ok);
  EXPECT_EQ(ledger->available().paise(), 1'02'000'00);
  EXPECT_EQ(ledger->allocated().paise(), 0);
  EXPECT_EQ(ledger->realized().paise(), 2'000'00);
  EXPECT_EQ(ledger->settlement().paise(), 1'02'000'00);
  EXPECT_TRUE(ledger->settled());

  ASSERT_TRUE(mgr.return_capital(wb, borrow.id, ledger->settlement()).ok);
  EXPECT_EQ(mgr.get_workbook(wb)->available_capital.paise(), 10'02'000'00);
}

TEST(PortfolioLedger, PaperDoesNotReduceAvailableUntilReal) {
  WorkbookManager mgr;
  const auto wb = mgr.create(UserId::from_u64(1), "A", Capital::from_paise(10'00'000'00));
  const auto borrow = mgr.borrow_capital(wb, Capital::from_paise(1'00'000'00));
  auto* ledger = mgr.activity(borrow.id);
  CapitalManager capital{*ledger};
  const auto id = ContainerId::from(1);

  ASSERT_TRUE(capital.allocate(id, Capital::from_paise(50'000'00), ContainerMode::Paper).ok);
  EXPECT_EQ(ledger->available().paise(), 1'00'000'00);
  EXPECT_EQ(ledger->paper().paise(), 50'000'00);
  EXPECT_EQ(ledger->allocated().paise(), 0);

  ASSERT_TRUE(capital.commit_real(id).ok);
  EXPECT_EQ(ledger->available().paise(), 50'000'00);
  EXPECT_EQ(ledger->paper().paise(), 0);
  EXPECT_EQ(ledger->allocated().paise(), 50'000'00);

  ASSERT_TRUE(capital.release(id, Capital::from_paise(48'000'00)).ok);
  EXPECT_EQ(ledger->available().paise(), 98'000'00);
  EXPECT_EQ(ledger->realized().paise(), -2'000'00);
  EXPECT_EQ(ledger->settlement().paise(), 98'000'00);
}

TEST(PortfolioLedger, PaperPnlReturnsToWorkbookOnSettle) {
  WorkbookManager mgr;
  const auto wb = mgr.create(UserId::from_u64(1), "A", Capital::from_paise(10'00'000'00));
  const auto borrow = mgr.borrow_capital(wb, Capital::from_paise(1'00'000'00));
  auto* ledger = mgr.activity(borrow.id);
  CapitalManager capital{*ledger};
  const auto id = ContainerId::from(3);
  ASSERT_TRUE(capital.allocate(id, Capital::from_paise(50'000'00), ContainerMode::Backtest).ok);
  ASSERT_TRUE(capital.release(id, Capital::from_paise(58'000'00)).ok);
  EXPECT_EQ(ledger->available().paise(), 1'00'000'00);
  EXPECT_EQ(ledger->paper().paise(), 0);
  EXPECT_EQ(ledger->realized().paise(), 8'000'00);
  EXPECT_EQ(ledger->settlement().paise(), 1'08'000'00);
  ASSERT_TRUE(mgr.return_capital(wb, borrow.id, ledger->settlement()).ok);
  EXPECT_EQ(mgr.get_workbook(wb)->available_capital.paise(), 10'08'000'00);
}

TEST(PortfolioLedger, InsufficientAndDoubleAllocate) {
  WorkbookManager mgr;
  const auto wb = mgr.create(UserId::from_u64(1), "A", Capital::from_paise(1000));
  const auto borrow = mgr.borrow_capital(wb, Capital::from_paise(500));
  auto* ledger = mgr.activity(borrow.id);
  CapitalManager capital{*ledger};
  const auto id = ContainerId::from(1);
  EXPECT_FALSE(capital.allocate(id, Capital::from_paise(501), ContainerMode::Real).ok);
  ASSERT_TRUE(capital.allocate(id, Capital::from_paise(200), ContainerMode::Real).ok);
  EXPECT_FALSE(capital.allocate(id, Capital::from_paise(100), ContainerMode::Real).ok);
  EXPECT_FALSE(capital.allocate(ContainerId::from(2), Capital::from_paise(400), ContainerMode::Real)
                   .ok);
  EXPECT_FALSE(capital.release(ContainerId::from(99), Capital::from_paise(1)).ok);
}

TEST(PortfolioLedger, TwoContainersIndependent) {
  WorkbookManager mgr;
  const auto wb = mgr.create(UserId::from_u64(1), "A", Capital::from_paise(10'00'000'00));
  const auto borrow = mgr.borrow_capital(wb, Capital::from_paise(1'00'000'00));
  auto* ledger = mgr.activity(borrow.id);
  CapitalManager capital{*ledger};
  ASSERT_TRUE(capital.allocate(ContainerId::from(1), Capital::from_paise(40'000'00),
                               ContainerMode::Real)
                  .ok);
  ASSERT_TRUE(capital.allocate(ContainerId::from(2), Capital::from_paise(30'000'00),
                               ContainerMode::Paper)
                  .ok);
  EXPECT_EQ(ledger->available().paise(), 60'000'00);
  EXPECT_EQ(ledger->allocated().paise(), 40'000'00);
  EXPECT_EQ(ledger->paper().paise(), 30'000'00);

  ASSERT_TRUE(capital.release(ContainerId::from(2), Capital::from_paise(31'000'00)).ok);
  ASSERT_TRUE(capital.release(ContainerId::from(1), Capital::from_paise(39'000'00)).ok);
  EXPECT_EQ(ledger->available().paise(), 99'000'00);
  EXPECT_EQ(ledger->realized().paise(), 0);
  EXPECT_EQ(ledger->settlement().paise(), 1'00'000'00);
}
