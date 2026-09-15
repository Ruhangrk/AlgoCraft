#pragma once

#include <memory>
#include <vector>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/indicators/indicator_library.hpp"
#include "algocraft/portfolio/capital_manager.hpp"
#include "algocraft/portfolio/op_result.hpp"
#include "algocraft/risk/risk_engine.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

class ExecutionVenue;

struct TradingContainerConfig {
  ContainerId id{};
  WorkbookId workbook_id{};
  SymbolId symbol_id{0};
  StrategyId strategy_id{};
  TradingMode trading_mode{TradingMode::Mis};
  ContainerMode mode{ContainerMode::Backtest};
  Capital sim_cash{};
  Capital real_allocation{};
  StrategyConfig strategy{};
};

class TradingContainer {
public:
  TradingContainer(TradingContainerConfig config, std::unique_ptr<Strategy> strategy,
                   ExecutionVenue& venue, RiskEngine& risk, CapitalManager* capital = nullptr);

  void warmup(const std::vector<BarEvent>& bars);
  OpResult start();
  OpResult upgrade(ContainerMode new_mode);
  void exit();
  void force_exit(Price price);
  void stop();

  void on_bar(const BarEvent& bar);
  void on_fill(const FillEvent& fill);
  void on_order_update(const OrderUpdate& update);
  void on_system_event(const SystemEvent& event);

  [[nodiscard]] ContainerId id() const { return config_.id; }
  [[nodiscard]] ContainerMode mode() const { return config_.mode; }
  [[nodiscard]] ContainerStatus status() const { return status_; }
  [[nodiscard]] Capital cash() const { return cash_; }
  [[nodiscard]] Quantity position() const { return position_; }
  [[nodiscard]] Capital realized() const { return realized_; }
  [[nodiscard]] RiskResult last_rejection() const { return last_rejection_; }

private:
  void apply_fill(const FillEvent& fill);
  void flatten(Price price, bool bypass_risk);
  [[nodiscard]] ContainerContext context() const;

  TradingContainerConfig config_{};
  std::unique_ptr<Strategy> strategy_{};
  ExecutionVenue& venue_;
  RiskEngine& risk_;
  CapitalManager* capital_{nullptr};
  IndicatorLibrary lib_{};
  ContainerStatus status_{ContainerStatus::WarmingUp};
  Capital cash_{};
  Quantity position_{};
  Price avg_entry_{};
  Capital realized_{};
  BarEvent last_bar_{};
  bool have_bar_{false};
  bool registered_{false};
  std::vector<OrderIntent> intents_{};
  RiskResult last_rejection_{};
};

}  // namespace algocraft
