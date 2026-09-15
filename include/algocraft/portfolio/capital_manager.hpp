#pragma once

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/portfolio/op_result.hpp"
#include "algocraft/portfolio/portfolio_ledger.hpp"

namespace algocraft {

class CapitalManager {
public:
  explicit CapitalManager(PortfolioLedger& ledger);

  OpResult allocate(ContainerId id, Capital amount, ContainerMode mode);
  OpResult commit_real(ContainerId id);
  OpResult release(ContainerId id, Capital returned_value);

  [[nodiscard]] PortfolioLedger& ledger() { return ledger_; }
  [[nodiscard]] const PortfolioLedger& ledger() const { return ledger_; }

private:
  PortfolioLedger& ledger_;
};

}  // namespace algocraft
