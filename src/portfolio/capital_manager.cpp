#include "algocraft/portfolio/capital_manager.hpp"

namespace algocraft {

CapitalManager::CapitalManager(PortfolioLedger& ledger) : ledger_{ledger} {}

OpResult CapitalManager::allocate(ContainerId id, Capital amount, ContainerMode mode) {
  return ledger_.take(id, amount, mode);
}

OpResult CapitalManager::commit_real(ContainerId id) { return ledger_.commit_real(id); }

OpResult CapitalManager::release(ContainerId id, Capital returned_value) {
  return ledger_.give_back(id, returned_value);
}

}  // namespace algocraft
