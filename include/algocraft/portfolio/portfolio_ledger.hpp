#pragma once

#include <map>
#include <vector>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/position.hpp"
#include "algocraft/portfolio/op_result.hpp"

namespace algocraft {

struct ContainerAllocation {
  Capital principal{};
  bool real{false};
};

class PortfolioLedger {
public:
  PortfolioLedger(BorrowId borrow_id, Capital borrowed);

  [[nodiscard]] BorrowId borrow_id() const { return borrow_id_; }
  [[nodiscard]] Capital borrowed() const { return borrowed_; }
  [[nodiscard]] Capital available() const { return available_; }
  [[nodiscard]] Capital allocated() const { return allocated_; }
  [[nodiscard]] Capital paper() const { return paper_; }
  [[nodiscard]] Capital realized() const { return realized_; }
  [[nodiscard]] Capital unrealized() const { return unrealized_; }
  [[nodiscard]] Capital settlement() const { return borrowed_ + realized_; }
  [[nodiscard]] bool settled() const {
    return allocated_.paise() == 0 && paper_.paise() == 0;
  }

  [[nodiscard]] const ContainerAllocation* allocation(ContainerId id) const;
  [[nodiscard]] Position position(SymbolId symbol_id) const;
  [[nodiscard]] const std::vector<FillEvent>& fills() const { return fills_; }

  OpResult take(ContainerId id, Capital amount, ContainerMode mode);
  OpResult commit_real(ContainerId id);
  OpResult give_back(ContainerId id, Capital returned_value);

  void apply_fill(const FillEvent& fill);
  void set_unrealized(Capital value);

private:
  BorrowId borrow_id_{};
  Capital borrowed_{};
  Capital available_{};
  Capital allocated_{};
  Capital paper_{};
  Capital realized_{};
  Capital unrealized_{};
  std::map<ContainerId, ContainerAllocation> allocs_{};
  std::map<SymbolId, Position> positions_{};
  std::vector<FillEvent> fills_{};
};

}  // namespace algocraft
