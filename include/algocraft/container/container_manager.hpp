#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "algocraft/container/trading_container.hpp"
#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/portfolio/capital_manager.hpp"
#include "algocraft/portfolio/op_result.hpp"
#include "algocraft/risk/risk_engine.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

namespace algocraft {

class ExecutionVenue;

class ContainerManager {
public:
  struct CreateRequest {
    SymbolId symbol_id{0};
    StrategyId strategy_id{};
    std::string strategy_name;
    Capital allocation{};
    ContainerMode mode{ContainerMode::Backtest};
    Price last_price{};
  };

  ContainerManager(WorkbookId workbook_id, ExecutionVenue& venue, RiskEngine& risk,
                   CapitalManager& capital, StrategyRegistry& strategies);

  std::optional<ContainerId> create(const CreateRequest& req);
  void kill(ContainerId id);
  OpResult upgrade(ContainerId id, ContainerMode new_mode);

  void on_bar(const BarEvent& bar);
  void on_system_event(const SystemEvent& event);
  void force_exit_remaining();

  struct Snapshot {
    ContainerId id{};
    SymbolId symbol_id{0};
    std::string ticker;
    std::string strategy_name;
    ContainerMode mode{ContainerMode::Backtest};
    Capital allocation{};
    Capital cash{};
    Capital realized{};
    int fills{0};
  };

  [[nodiscard]] Capital available_capital() const;
  [[nodiscard]] Capital allocated_capital() const;
  [[nodiscard]] int real_count() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::vector<Snapshot> snapshots() const;

  struct SignalLog {
    ContainerId container_id{};
    SymbolId symbol_id{0};
    std::string strategy_name;
    Timestamp timestamp{};
    int intent_count{0};
    std::string indicators_json;
  };
  struct RejectionLog {
    ContainerId container_id{};
    SymbolId symbol_id{0};
    std::string strategy_name;
    Timestamp timestamp{};
    std::string rule;
    std::string reason;
  };

  [[nodiscard]] std::vector<SignalLog> collect_signals() const;
  [[nodiscard]] std::vector<RejectionLog> collect_rejections() const;

private:
  TradingContainer* find(ContainerId id);
  const TradingContainer* find(ContainerId id) const;

  WorkbookId workbook_id_{};
  ExecutionVenue& venue_;
  RiskEngine& risk_;
  CapitalManager& capital_;
  StrategyRegistry& strategies_;
  std::vector<std::unique_ptr<TradingContainer>> containers_{};
  std::uint64_t next_id_{1};
};

}  // namespace algocraft
