#include "algocraft/workbook/workbook_manager.hpp"

#include <cstdint>

#include <gtest/gtest.h>

using algocraft::BorrowId;
using algocraft::Capital;
using algocraft::UserId;
using algocraft::WorkbookEventType;
using algocraft::WorkbookManager;
using algocraft::WorkbookStatus;

namespace {

UserId user(std::uint64_t n) { return UserId::from_u64(n); }

}  // namespace

TEST(WorkbookManager, CreateAddBorrowReturnProfit) {
  WorkbookManager mgr;
  const auto id = mgr.create(user(1), "NSE Momentum", Capital::from_paise(10'00'000'00));
  auto book = mgr.get_workbook(id);
  ASSERT_TRUE(book.has_value());
  EXPECT_EQ(book->main_capital.paise(), 10'00'000'00);
  EXPECT_EQ(book->available_capital.paise(), 10'00'000'00);
  EXPECT_EQ(book->borrowed_capital.paise(), 0);

  ASSERT_TRUE(mgr.add_capital(id, Capital::from_paise(2'00'000'00)).ok);
  book = mgr.get_workbook(id);
  EXPECT_EQ(book->main_capital.paise(), 12'00'000'00);
  EXPECT_EQ(book->available_capital.paise(), 12'00'000'00);

  const auto borrow = mgr.borrow_capital(id, Capital::from_paise(5'00'000'00));
  ASSERT_TRUE(borrow.result.ok);
  book = mgr.get_workbook(id);
  EXPECT_EQ(book->available_capital.paise(), 7'00'000'00);
  EXPECT_EQ(book->borrowed_capital.paise(), 5'00'000'00);

  ASSERT_TRUE(mgr.return_capital(id, borrow.id, Capital::from_paise(5'08'000'00)).ok);
  book = mgr.get_workbook(id);
  EXPECT_EQ(book->available_capital.paise(), 12'08'000'00);
  EXPECT_EQ(book->borrowed_capital.paise(), 0);
  EXPECT_EQ(book->main_capital.paise(), 12'00'000'00);

  const auto ev = mgr.events(id);
  ASSERT_GE(ev.size(), 4u);
  EXPECT_EQ(ev[0].type, WorkbookEventType::Created);
  EXPECT_EQ(ev[1].type, WorkbookEventType::CapitalAdded);
  EXPECT_EQ(ev[2].type, WorkbookEventType::ActivityBorrow);
  EXPECT_EQ(ev[3].type, WorkbookEventType::ActivityReturn);
}

TEST(WorkbookManager, ReturnLossAndIndependentActivities) {
  WorkbookManager mgr;
  const auto id = mgr.create(user(1), "A", Capital::from_paise(10'00'000'00));
  const auto a = mgr.borrow_capital(id, Capital::from_paise(5'00'000'00));
  const auto b = mgr.borrow_capital(id, Capital::from_paise(1'00'000'00));
  ASSERT_TRUE(a.result.ok);
  ASSERT_TRUE(b.result.ok);
  auto book = mgr.get_workbook(id);
  EXPECT_EQ(book->available_capital.paise(), 4'00'000'00);
  EXPECT_EQ(book->borrowed_capital.paise(), 6'00'000'00);

  ASSERT_TRUE(mgr.return_capital(id, b.id, Capital::from_paise(1'08'000'00)).ok);
  book = mgr.get_workbook(id);
  EXPECT_EQ(book->available_capital.paise(), 5'08'000'00);

  ASSERT_TRUE(mgr.return_capital(id, a.id, Capital::from_paise(4'88'000'00)).ok);
  book = mgr.get_workbook(id);
  EXPECT_EQ(book->available_capital.paise(), 9'96'000'00);
  EXPECT_EQ(book->borrowed_capital.paise(), 0);
}

TEST(WorkbookManager, BorrowMoreThanAvailableFails) {
  WorkbookManager mgr;
  const auto id = mgr.create(user(1), "A", Capital::from_paise(100));
  const auto borrow = mgr.borrow_capital(id, Capital::from_paise(101));
  EXPECT_FALSE(borrow.result.ok);
  EXPECT_STREQ(borrow.result.error, "insufficient funds");
  EXPECT_EQ(mgr.get_workbook(id)->available_capital.paise(), 100);
}

TEST(WorkbookManager, CannotReturnTwiceOrUnknown) {
  WorkbookManager mgr;
  const auto id = mgr.create(user(1), "A", Capital::from_paise(1000));
  const auto borrow = mgr.borrow_capital(id, Capital::from_paise(400));
  ASSERT_TRUE(mgr.return_capital(id, borrow.id, Capital::from_paise(400)).ok);
  EXPECT_FALSE(mgr.return_capital(id, borrow.id, Capital::from_paise(400)).ok);
  EXPECT_FALSE(mgr.return_capital(id, BorrowId::from(99), Capital::from_paise(1)).ok);
}

TEST(WorkbookManager, SoftDeleteHidesFromList) {
  WorkbookManager mgr;
  const auto keep = mgr.create(user(1), "keep", Capital::from_paise(1));
  const auto gone = mgr.create(user(1), "gone", Capital::from_paise(1));
  mgr.create(user(2), "other", Capital::from_paise(1));
  ASSERT_TRUE(mgr.soft_delete(gone).ok);
  const auto listed = mgr.list_workbooks(user(1));
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].id, keep);
  EXPECT_FALSE(mgr.get_workbook(gone).has_value());
  EXPECT_FALSE(mgr.add_capital(gone, Capital::from_paise(1)).ok);
  EXPECT_FALSE(mgr.borrow_capital(gone, Capital::from_paise(1)).result.ok);
  EXPECT_EQ(listed[0].status, WorkbookStatus::Active);
}

TEST(WorkbookManager, InvalidAmounts) {
  WorkbookManager mgr;
  const auto id = mgr.create(user(1), "A", Capital::from_paise(100));
  EXPECT_FALSE(mgr.add_capital(id, Capital::from_paise(0)).ok);
  EXPECT_FALSE(mgr.borrow_capital(id, Capital::from_paise(0)).result.ok);
  EXPECT_FALSE(mgr.add_capital(algocraft::WorkbookId::from_u64(99), Capital::from_paise(1)).ok);
}

TEST(WorkbookManager, CannotReturnWhileContainersAllocated) {
  WorkbookManager mgr;
  const auto id = mgr.create(user(1), "A", Capital::from_paise(10'00'000'00));
  const auto borrow = mgr.borrow_capital(id, Capital::from_paise(1'00'000'00));
  auto* ledger = mgr.activity(borrow.id);
  ASSERT_NE(ledger, nullptr);
  ASSERT_TRUE(
      ledger->take(algocraft::ContainerId::from(1), Capital::from_paise(40'000'00),
                   algocraft::ContainerMode::Real)
          .ok);
  EXPECT_FALSE(mgr.return_capital(id, borrow.id, Capital::from_paise(1'00'000'00)).ok);
  ASSERT_TRUE(ledger->give_back(algocraft::ContainerId::from(1), Capital::from_paise(40'000'00)).ok);
  EXPECT_TRUE(mgr.return_capital(id, borrow.id, ledger->settlement()).ok);
}
