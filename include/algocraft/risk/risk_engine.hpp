#pragma once

#include <map>
#include <memory>
#include <vector>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/portfolio/op_result.hpp"

namespace algocraft {

class PortfolioLedger;

enum class RiskDecision : std::uint8_t { Approved = 0, Rejected };

struct RiskResult {
  RiskDecision decision{RiskDecision::Approved};
  const char* rule{"none"};
  const char* reason{"ok"};

  static RiskResult approved() { return {}; }
  static RiskResult rejected(const char* rule, const char* reason) {
    return {RiskDecision::Rejected, rule, reason};
  }

  [[nodiscard]] bool ok() const { return decision == RiskDecision::Approved; }
};

struct RiskLimits {
  Quantity max_position{Quantity::from_shares(1'000'000)};
  Capital daily_loss_limit{Capital::from_paise(10'00'000'00)};
  int leverage{5};
};

struct ContainerContext {
  WorkbookId workbook_id{};
  ContainerId container_id{};
  StrategyId strategy_id{};
  SymbolId symbol_id{0};
  TradingMode trading_mode{TradingMode::Mis};
  ContainerMode mode{ContainerMode::Backtest};
  Capital allocated{};
  Capital cash{};
  Quantity position{};
  Price avg_entry{};
  Capital daily_pnl{};
  Timestamp now{};
};

struct ActiveContainerIndex {
  struct Key {
    WorkbookId workbook_id{};
    SymbolId symbol_id{0};
    StrategyId strategy_id{};

    auto operator<=>(const Key&) const = default;
  };

  [[nodiscard]] bool occupied(const Key& key, ContainerId self) const;

  std::map<Key, ContainerId> by_key{};
  std::map<ContainerId, Key> by_id{};
};

struct RiskSnapshot {
  bool kill_switch{false};
  bool mis_squareoff{false};
  RiskLimits limits{};
  const PortfolioLedger* ledger{nullptr};
  const ActiveContainerIndex* active{nullptr};
};

class RiskRule {
public:
  virtual ~RiskRule() = default;
  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                         const RiskSnapshot& snap) const = 0;
  [[nodiscard]] virtual RiskResult check_start(const ContainerContext& ctx,
                                               const RiskSnapshot& snap) const {
    (void)ctx;
    (void)snap;
    return RiskResult::approved();
  }
};

class KillSwitchRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "KillSwitch"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
};

class MarketHoursRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "MarketHours"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
};

class MaxPositionSizeRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "MaxPositionSize"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
};

class DailyLossLimitRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "DailyLossLimit"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
};

class ContainerCapitalRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "ContainerCapital"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
};

class DuplicateContainerRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "DuplicateContainer"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
  [[nodiscard]] RiskResult check_start(const ContainerContext& ctx,
                                       const RiskSnapshot& snap) const override;
};

class MISSquareOffRule final : public RiskRule {
public:
  [[nodiscard]] const char* name() const override { return "MISSquareOff"; }
  [[nodiscard]] RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                                 const RiskSnapshot& snap) const override;
};

class RiskEngine {
public:
  explicit RiskEngine(RiskLimits limits = {});

  void add_rule(std::unique_ptr<RiskRule> rule);
  void set_kill_switch(bool armed);
  void set_mis_squareoff(bool active);
  void on_system_event(const SystemEvent& event);

  [[nodiscard]] bool kill_switch() const { return kill_switch_; }
  [[nodiscard]] bool mis_squareoff() const { return mis_squareoff_; }
  [[nodiscard]] const RiskLimits& limits() const { return limits_; }

  RiskResult check(const OrderIntent& intent, const ContainerContext& ctx,
                   const PortfolioLedger* ledger = nullptr) const;
  RiskResult check_start(const ContainerContext& ctx) const;
  OpResult register_container(const ContainerContext& ctx);
  void unregister_container(ContainerId id);

private:
  RiskSnapshot snapshot(const PortfolioLedger* ledger) const;

  RiskLimits limits_{};
  bool kill_switch_{false};
  bool mis_squareoff_{false};
  std::vector<std::unique_ptr<RiskRule>> rules_{};
  ActiveContainerIndex active_{};
};

}  // namespace algocraft
