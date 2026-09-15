#include "algocraft/portfolio/portfolio_ledger.hpp"

#include <cstdint>

#include "algocraft/domain/capital.hpp"

namespace algocraft {

PortfolioLedger::PortfolioLedger(BorrowId borrow_id, Capital borrowed)
    : borrow_id_{borrow_id}, borrowed_{borrowed}, available_{borrowed} {}

const ContainerAllocation* PortfolioLedger::allocation(ContainerId id) const {
  const auto it = allocs_.find(id);
  if (it == allocs_.end()) {
    return nullptr;
  }
  return &it->second;
}

Position PortfolioLedger::position(SymbolId symbol_id) const {
  const auto it = positions_.find(symbol_id);
  if (it == positions_.end()) {
    return {};
  }
  return it->second;
}

OpResult PortfolioLedger::take(ContainerId id, Capital amount, ContainerMode mode) {
  if (amount.paise() <= 0) {
    return OpResult::fail("invalid amount");
  }
  if (allocs_.contains(id)) {
    return OpResult::fail("already allocated");
  }
  const bool real = mode == ContainerMode::Real;
  if (real && available_.paise() < amount.paise()) {
    return OpResult::fail("insufficient funds");
  }
  ContainerAllocation alloc{};
  alloc.principal = amount;
  alloc.real = real;
  if (real) {
    available_ = available_ - amount;
    allocated_ = allocated_ + amount;
  } else {
    paper_ = paper_ + amount;
  }
  allocs_.emplace(id, alloc);
  return OpResult::success();
}

OpResult PortfolioLedger::commit_real(ContainerId id) {
  const auto it = allocs_.find(id);
  if (it == allocs_.end()) {
    return OpResult::fail("not allocated");
  }
  if (it->second.real) {
    return OpResult::fail("already real");
  }
  const auto amount = it->second.principal;
  if (available_.paise() < amount.paise()) {
    return OpResult::fail("insufficient funds");
  }
  available_ = available_ - amount;
  paper_ = paper_ - amount;
  allocated_ = allocated_ + amount;
  it->second.real = true;
  return OpResult::success();
}

OpResult PortfolioLedger::give_back(ContainerId id, Capital returned_value) {
  const auto it = allocs_.find(id);
  if (it == allocs_.end()) {
    return OpResult::fail("not allocated");
  }
  const auto principal = it->second.principal;
  const auto pnl = Capital::from_paise(returned_value.paise() - principal.paise());
  realized_ = realized_ + pnl;
  if (it->second.real) {
    allocated_ = allocated_ - principal;
    available_ = available_ + returned_value;
  } else {
    paper_ = paper_ - principal;
  }
  allocs_.erase(it);
  return OpResult::success();
}

void PortfolioLedger::apply_fill(const FillEvent& fill) {
  fills_.push_back(fill);
  auto& pos = positions_[fill.symbol_id];
  pos.symbol_id = fill.symbol_id;
  if (fill.side == Side::Buy) {
    if (pos.net_qty.shares() == 0) {
      pos.net_qty = fill.filled_qty;
      pos.average_price = fill.fill_price;
    } else {
      const auto old_n = pos.average_price.paise() * pos.net_qty.shares();
      const auto add_n = fill.fill_price.paise() * fill.filled_qty.shares();
      const auto new_q = pos.net_qty.shares() + fill.filled_qty.shares();
      pos.net_qty = Quantity::from_shares(new_q);
      pos.average_price = Price::from_paise((old_n + add_n) / new_q);
    }
    return;
  }
  const auto qty = fill.filled_qty.shares();
  const auto pnl = notional(fill.fill_price, fill.filled_qty).paise() -
                   notional(pos.average_price, fill.filled_qty).paise() - fill.fees.paise();
  pos.realized_pnl = Capital::from_paise(pos.realized_pnl.paise() + pnl);
  const auto left = pos.net_qty.shares() - qty;
  pos.net_qty = Quantity::from_shares(left < 0 ? 0 : left);
  if (pos.net_qty.shares() == 0) {
    pos.average_price = {};
  }
}

void PortfolioLedger::set_unrealized(Capital value) { unrealized_ = value; }

}  // namespace algocraft
