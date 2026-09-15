#include "algocraft/risk/risk_engine.hpp"

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/execution/cost_calculator.hpp"

namespace algocraft {
namespace {

ActiveContainerIndex::Key make_key(const ContainerContext& ctx) {
  ActiveContainerIndex::Key key{};
  key.workbook_id = ctx.workbook_id;
  key.symbol_id = ctx.symbol_id;
  key.strategy_id = ctx.strategy_id;
  return key;
}

}  // namespace

bool ActiveContainerIndex::occupied(const Key& key, ContainerId self) const {
  const auto it = by_key.find(key);
  if (it == by_key.end()) {
    return false;
  }
  return it->second != self;
}

RiskResult KillSwitchRule::check(const OrderIntent&, const ContainerContext&,
                                 const RiskSnapshot& snap) const {
  if (snap.kill_switch) {
    return RiskResult::rejected(name(), "kill switch armed");
  }
  return RiskResult::approved();
}

RiskResult MarketHoursRule::check(const OrderIntent&, const ContainerContext& ctx,
                                  const RiskSnapshot&) const {
  if (!nse_regular_hours(ctx.now)) {
    return RiskResult::rejected(name(), "outside NSE regular hours");
  }
  return RiskResult::approved();
}

RiskResult MaxPositionSizeRule::check(const OrderIntent& intent, const ContainerContext& ctx,
                                      const RiskSnapshot& snap) const {
  if (intent.side != Side::Buy) {
    return RiskResult::approved();
  }
  const auto next = ctx.position.shares() + intent.quantity.shares();
  if (next > snap.limits.max_position.shares()) {
    return RiskResult::rejected(name(), "position exceeds max size");
  }
  return RiskResult::approved();
}

RiskResult DailyLossLimitRule::check(const OrderIntent&, const ContainerContext& ctx,
                                     const RiskSnapshot& snap) const {
  if (ctx.daily_pnl.paise() <= -snap.limits.daily_loss_limit.paise()) {
    return RiskResult::rejected(name(), "daily loss limit reached");
  }
  return RiskResult::approved();
}

RiskResult ContainerCapitalRule::check(const OrderIntent& intent, const ContainerContext& ctx,
                                       const RiskSnapshot& snap) const {
  if (intent.side != Side::Buy) {
    return RiskResult::approved();
  }
  if (intent.price.paise() <= 0) {
    return RiskResult::rejected(name(), "missing price");
  }
  const auto notion = notional(intent.price, intent.quantity);
  const auto leverage = snap.limits.leverage > 0 ? snap.limits.leverage : 1;
  const auto required = Capital::from_paise(notion.paise() / leverage);
  CostCalculator costs{};
  const auto fees = costs.buy_fees(intent.price, intent.quantity);
  if (ctx.cash.paise() < required.paise() + fees.paise()) {
    return RiskResult::rejected(name(), "exceeds container capital");
  }
  return RiskResult::approved();
}

RiskResult DuplicateContainerRule::check(const OrderIntent&, const ContainerContext&,
                                         const RiskSnapshot&) const {
  return RiskResult::approved();
}

RiskResult DuplicateContainerRule::check_start(const ContainerContext& ctx,
                                               const RiskSnapshot& snap) const {
  if (snap.active != nullptr && snap.active->occupied(make_key(ctx), ctx.container_id)) {
    return RiskResult::rejected(name(), "duplicate symbol+strategy in workbook");
  }
  return RiskResult::approved();
}

RiskResult MISSquareOffRule::check(const OrderIntent&, const ContainerContext& ctx,
                                   const RiskSnapshot& snap) const {
  if (snap.mis_squareoff && ctx.trading_mode == TradingMode::Mis) {
    return RiskResult::rejected(name(), "MIS square-off window");
  }
  return RiskResult::approved();
}

RiskEngine::RiskEngine(RiskLimits limits) : limits_{limits} {
  rules_.push_back(std::make_unique<KillSwitchRule>());
  rules_.push_back(std::make_unique<MISSquareOffRule>());
  rules_.push_back(std::make_unique<MarketHoursRule>());
  rules_.push_back(std::make_unique<DailyLossLimitRule>());
  rules_.push_back(std::make_unique<MaxPositionSizeRule>());
  rules_.push_back(std::make_unique<ContainerCapitalRule>());
  rules_.push_back(std::make_unique<DuplicateContainerRule>());
}

void RiskEngine::add_rule(std::unique_ptr<RiskRule> rule) { rules_.push_back(std::move(rule)); }

void RiskEngine::set_kill_switch(bool armed) { kill_switch_ = armed; }

void RiskEngine::set_mis_squareoff(bool active) { mis_squareoff_ = active; }

void RiskEngine::on_system_event(const SystemEvent& event) {
  if (event.type == SystemEventType::KillSwitch) {
    kill_switch_ = true;
  } else if (event.type == SystemEventType::MisSquareoffWarning) {
    mis_squareoff_ = true;
  } else if (event.type == SystemEventType::SessionStart) {
    mis_squareoff_ = false;
  }
}

RiskSnapshot RiskEngine::snapshot(const PortfolioLedger* ledger) const {
  RiskSnapshot snap{};
  snap.kill_switch = kill_switch_;
  snap.mis_squareoff = mis_squareoff_;
  snap.limits = limits_;
  snap.ledger = ledger;
  snap.active = &active_;
  return snap;
}

RiskResult RiskEngine::check(const OrderIntent& intent, const ContainerContext& ctx,
                             const PortfolioLedger* ledger) const {
  const auto snap = snapshot(ledger);
  for (const auto& rule : rules_) {
    const auto result = rule->check(intent, ctx, snap);
    if (!result.ok()) {
      return result;
    }
  }
  return RiskResult::approved();
}

RiskResult RiskEngine::check_start(const ContainerContext& ctx) const {
  const auto snap = snapshot(nullptr);
  for (const auto& rule : rules_) {
    const auto result = rule->check_start(ctx, snap);
    if (!result.ok()) {
      return result;
    }
  }
  return RiskResult::approved();
}

OpResult RiskEngine::register_container(const ContainerContext& ctx) {
  const auto blocked = check_start(ctx);
  if (!blocked.ok()) {
    return OpResult::fail(blocked.reason);
  }
  const auto key = make_key(ctx);
  active_.by_key[key] = ctx.container_id;
  active_.by_id[ctx.container_id] = key;
  return OpResult::success();
}

void RiskEngine::unregister_container(ContainerId id) {
  const auto it = active_.by_id.find(id);
  if (it == active_.by_id.end()) {
    return;
  }
  active_.by_key.erase(it->second);
  active_.by_id.erase(it);
}

}  // namespace algocraft
