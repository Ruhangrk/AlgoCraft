#include "algocraft/container/container_manager.hpp"

#include "algocraft/routing/position_sizer.hpp"

namespace algocraft {

ContainerManager::ContainerManager(WorkbookId workbook_id, ExecutionVenue& venue, RiskEngine& risk,
                                   CapitalManager& capital, StrategyRegistry& strategies)
    : workbook_id_{workbook_id},
      venue_{venue},
      risk_{risk},
      capital_{capital},
      strategies_{strategies} {}

std::optional<ContainerId> ContainerManager::create(const CreateRequest& req) {
  TradingContainerConfig cfg{};
  cfg.id = ContainerId::from(next_id_++);
  cfg.workbook_id = workbook_id_;
  cfg.symbol_id = req.symbol_id;
  cfg.strategy_id = req.strategy_id;
  cfg.mode = req.mode;
  cfg.real_allocation = req.allocation;
  cfg.sim_cash = req.allocation;
  cfg.strategy_name = req.strategy_name;
  cfg.strategy.symbol_id = req.symbol_id;
  cfg.strategy.order_qty = position_for_capital(req.allocation, req.last_price);
  cfg.strategy.clip_paise = 20'00'000'00;
  auto strategy = strategies_.create(req.strategy_name);
  auto container =
      std::make_unique<TradingContainer>(std::move(cfg), std::move(strategy), venue_, risk_,
                                         &capital_);
  const auto started = container->start();
  if (!started.ok) {
    return std::nullopt;
  }
  const auto id = container->id();
  containers_.push_back(std::move(container));
  return id;
}

void ContainerManager::kill(ContainerId id) {
  auto* container = find(id);
  if (container == nullptr || container->status() == ContainerStatus::Stopped) {
    return;
  }
  if (container->has_bar()) {
    container->force_exit(container->last_price());
  } else {
    container->stop();
  }
}

OpResult ContainerManager::upgrade(ContainerId id, ContainerMode new_mode) {
  auto* container = find(id);
  if (container == nullptr) {
    return OpResult::fail("unknown container");
  }
  return container->upgrade(new_mode);
}

void ContainerManager::on_bar(const BarEvent& bar) {
  for (auto& container : containers_) {
    if (container != nullptr && container->status() == ContainerStatus::Active) {
      container->on_bar(bar);
    }
  }
}

void ContainerManager::on_system_event(const SystemEvent& event) {
  for (auto& container : containers_) {
    if (container != nullptr) {
      container->on_system_event(event);
    }
  }
}

void ContainerManager::force_exit_remaining() {
  for (auto& container : containers_) {
    if (container == nullptr || container->status() == ContainerStatus::Stopped) {
      continue;
    }
    if (container->has_bar()) {
      container->force_exit(container->last_price());
    } else {
      container->stop();
    }
  }
}

Capital ContainerManager::available_capital() const { return capital_.ledger().available(); }

Capital ContainerManager::allocated_capital() const { return capital_.ledger().allocated(); }

int ContainerManager::real_count() const {
  int n = 0;
  for (const auto& container : containers_) {
    if (container != nullptr && container->mode() == ContainerMode::Real) {
      ++n;
    }
  }
  return n;
}

bool ContainerManager::empty() const { return containers_.empty(); }

std::vector<ContainerManager::Snapshot> ContainerManager::snapshots() const {
  std::vector<Snapshot> out;
  out.reserve(containers_.size());
  for (const auto& container : containers_) {
    if (container == nullptr) {
      continue;
    }
    Snapshot row{};
    row.id = container->id();
    row.symbol_id = container->symbol_id();
    row.strategy_name = container->strategy_name();
    row.mode = container->mode();
    row.allocation = container->allocation();
    row.cash = container->cash();
    row.realized = container->realized();
    row.fills = container->fills();
    out.push_back(row);
  }
  return out;
}

TradingContainer* ContainerManager::find(ContainerId id) {
  for (auto& container : containers_) {
    if (container != nullptr && container->id() == id) {
      return container.get();
    }
  }
  return nullptr;
}

const TradingContainer* ContainerManager::find(ContainerId id) const {
  for (const auto& container : containers_) {
    if (container != nullptr && container->id() == id) {
      return container.get();
    }
  }
  return nullptr;
}

}  // namespace algocraft
