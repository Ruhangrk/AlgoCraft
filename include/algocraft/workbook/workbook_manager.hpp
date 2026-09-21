#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/workbook.hpp"
#include "algocraft/portfolio/op_result.hpp"
#include "algocraft/portfolio/portfolio_ledger.hpp"

namespace algocraft {

struct BorrowOutcome {
  OpResult result{};
  BorrowId id{};
};

class WorkbookManager {
public:
  WorkbookId create(UserId user_id, std::string name, Capital initial_capital);
  // Seed an in-memory book at a known id (e.g. SQLite workbook row). No Created event.
  OpResult adopt(WorkbookId id, UserId user_id, std::string name, Capital main_capital,
                 Capital available_capital);
  OpResult add_capital(WorkbookId id, Capital amount);
  BorrowOutcome borrow_capital(WorkbookId id, Capital amount);
  OpResult return_capital(WorkbookId id, BorrowId borrow_id, Capital final_amount);
  OpResult soft_delete(WorkbookId id);

  [[nodiscard]] std::vector<Workbook> list_workbooks(UserId user_id) const;
  [[nodiscard]] std::optional<Workbook> get_workbook(WorkbookId id) const;
  [[nodiscard]] std::vector<WorkbookEvent> events(WorkbookId id) const;
  [[nodiscard]] PortfolioLedger* activity(BorrowId borrow_id);
  [[nodiscard]] const PortfolioLedger* activity(BorrowId borrow_id) const;

private:
  struct Record {
    Workbook book{};
    std::vector<WorkbookEvent> events{};
  };

  struct Borrow {
    BorrowId id{};
    WorkbookId workbook_id{};
    Capital original{};
    bool returned{false};
  };

  Workbook* mutable_book(WorkbookId id);
  const Workbook* book(WorkbookId id) const;
  void push_event(WorkbookId id, WorkbookEventType type, Capital amount);

  std::map<WorkbookId, Record> books_{};
  std::map<BorrowId, Borrow> borrows_{};
  std::map<BorrowId, PortfolioLedger> ledgers_{};
  std::uint64_t next_workbook_{1};
  std::uint64_t next_borrow_{1};
};

}  // namespace algocraft
