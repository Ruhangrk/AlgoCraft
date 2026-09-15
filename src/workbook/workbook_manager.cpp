#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft {

WorkbookId WorkbookManager::create(UserId user_id, std::string name, Capital initial_capital) {
  if (initial_capital.paise() < 0) {
    initial_capital = Capital{};
  }
  Record rec{};
  rec.book.id = WorkbookId::from_u64(next_workbook_++);
  rec.book.user_id = user_id;
  rec.book.name = std::move(name);
  rec.book.main_capital = initial_capital;
  rec.book.available_capital = initial_capital;
  rec.book.status = WorkbookStatus::Active;
  const auto id = rec.book.id;
  books_.emplace(id, std::move(rec));
  push_event(id, WorkbookEventType::Created, initial_capital);
  return id;
}

OpResult WorkbookManager::add_capital(WorkbookId id, Capital amount) {
  auto* wb = mutable_book(id);
  if (wb == nullptr) {
    return OpResult::fail("workbook not found");
  }
  if (wb->deleted_at.nanos() != 0) {
    return OpResult::fail("workbook deleted");
  }
  if (amount.paise() <= 0) {
    return OpResult::fail("invalid amount");
  }
  wb->main_capital = wb->main_capital + amount;
  wb->available_capital = wb->available_capital + amount;
  push_event(id, WorkbookEventType::CapitalAdded, amount);
  return OpResult::success();
}

BorrowOutcome WorkbookManager::borrow_capital(WorkbookId id, Capital amount) {
  auto* wb = mutable_book(id);
  if (wb == nullptr) {
    return {OpResult::fail("workbook not found"), {}};
  }
  if (wb->deleted_at.nanos() != 0) {
    return {OpResult::fail("workbook deleted"), {}};
  }
  if (amount.paise() <= 0) {
    return {OpResult::fail("invalid amount"), {}};
  }
  if (wb->available_capital.paise() < amount.paise()) {
    return {OpResult::fail("insufficient funds"), {}};
  }
  wb->available_capital = wb->available_capital - amount;
  wb->borrowed_capital = wb->borrowed_capital + amount;
  Borrow borrow{};
  borrow.id = BorrowId::from(next_borrow_++);
  borrow.workbook_id = id;
  borrow.original = amount;
  const auto borrow_id = borrow.id;
  borrows_.emplace(borrow_id, borrow);
  ledgers_.emplace(borrow_id, PortfolioLedger{borrow_id, amount});
  push_event(id, WorkbookEventType::ActivityBorrow, amount);
  return {OpResult::success(), borrow_id};
}

OpResult WorkbookManager::return_capital(WorkbookId id, BorrowId borrow_id, Capital final_amount) {
  auto* wb = mutable_book(id);
  if (wb == nullptr) {
    return OpResult::fail("workbook not found");
  }
  const auto it = borrows_.find(borrow_id);
  if (it == borrows_.end() || it->second.workbook_id != id) {
    return OpResult::fail("borrow not found");
  }
  if (it->second.returned) {
    return OpResult::fail("already returned");
  }
  if (final_amount.paise() < 0) {
    return OpResult::fail("invalid amount");
  }
  auto ledger_it = ledgers_.find(borrow_id);
  if (ledger_it != ledgers_.end() && !ledger_it->second.settled()) {
    return OpResult::fail("containers still allocated");
  }
  wb->available_capital = wb->available_capital + final_amount;
  wb->borrowed_capital = wb->borrowed_capital - it->second.original;
  it->second.returned = true;
  push_event(id, WorkbookEventType::ActivityReturn, final_amount);
  return OpResult::success();
}

OpResult WorkbookManager::soft_delete(WorkbookId id) {
  auto* wb = mutable_book(id);
  if (wb == nullptr) {
    return OpResult::fail("workbook not found");
  }
  if (wb->deleted_at.nanos() != 0) {
    return OpResult::fail("already deleted");
  }
  wb->deleted_at = Timestamp::now();
  wb->status = WorkbookStatus::Archived;
  return OpResult::success();
}

std::vector<Workbook> WorkbookManager::list_workbooks(UserId user_id) const {
  std::vector<Workbook> out;
  for (const auto& [id, rec] : books_) {
    (void)id;
    if (rec.book.user_id == user_id && rec.book.deleted_at.nanos() == 0) {
      out.push_back(rec.book);
    }
  }
  return out;
}

std::optional<Workbook> WorkbookManager::get_workbook(WorkbookId id) const {
  const auto* wb = book(id);
  if (wb == nullptr || wb->deleted_at.nanos() != 0) {
    return std::nullopt;
  }
  return *wb;
}

std::vector<WorkbookEvent> WorkbookManager::events(WorkbookId id) const {
  const auto it = books_.find(id);
  if (it == books_.end()) {
    return {};
  }
  return it->second.events;
}

PortfolioLedger* WorkbookManager::activity(BorrowId borrow_id) {
  const auto it = ledgers_.find(borrow_id);
  if (it == ledgers_.end()) {
    return nullptr;
  }
  return &it->second;
}

const PortfolioLedger* WorkbookManager::activity(BorrowId borrow_id) const {
  const auto it = ledgers_.find(borrow_id);
  if (it == ledgers_.end()) {
    return nullptr;
  }
  return &it->second;
}

Workbook* WorkbookManager::mutable_book(WorkbookId id) {
  const auto it = books_.find(id);
  if (it == books_.end()) {
    return nullptr;
  }
  return &it->second.book;
}

const Workbook* WorkbookManager::book(WorkbookId id) const {
  const auto it = books_.find(id);
  if (it == books_.end()) {
    return nullptr;
  }
  return &it->second.book;
}

void WorkbookManager::push_event(WorkbookId id, WorkbookEventType type, Capital amount) {
  const auto it = books_.find(id);
  if (it == books_.end()) {
    return;
  }
  WorkbookEvent ev{};
  ev.type = type;
  ev.workbook_id = id;
  ev.amount = amount;
  ev.timestamp = Timestamp::now();
  it->second.events.push_back(ev);
}

}  // namespace algocraft
