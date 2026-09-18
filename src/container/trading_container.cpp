#include "algocraft/container/trading_container.hpp"

#include <string>

#include "algocraft/execution/execution_venue.hpp"
#include "algocraft/strategies/make_intent.hpp"

namespace algocraft {

TradingContainer::TradingContainer(TradingContainerConfig config, std::unique_ptr<Strategy> strategy,
                                   ExecutionVenue& venue, RiskEngine& risk, CapitalManager* capital)
    : config_{std::move(config)},
      strategy_{std::move(strategy)},
      venue_{venue},
      risk_{risk},
      capital_{capital} {
  config_.strategy.symbol_id = config_.symbol_id;
  if (strategy_) {
    strategy_->configure(config_.strategy, lib_);
  }
  cash_ = config_.mode == ContainerMode::Real ? config_.real_allocation : config_.sim_cash;
  intents_.reserve(8);
}

void TradingContainer::warmup(const std::vector<BarEvent>& bars) {
  status_ = ContainerStatus::WarmingUp;
  for (const auto& bar : bars) {
    on_bar(bar);
  }
}

OpResult TradingContainer::start() {
  if (status_ == ContainerStatus::Active) {
    return OpResult::fail("already started");
  }
  if (status_ == ContainerStatus::Stopped) {
    return OpResult::fail("stopped");
  }
  auto ctx = context();
  const auto dup = risk_.register_container(ctx);
  if (!dup.ok) {
    return dup;
  }
  registered_ = true;
  if (capital_ != nullptr) {
    const auto amount =
        config_.mode == ContainerMode::Real ? config_.real_allocation : config_.sim_cash;
    const auto alloc = capital_->allocate(config_.id, amount, config_.mode);
    if (!alloc.ok) {
      risk_.unregister_container(config_.id);
      registered_ = false;
      return alloc;
    }
  }
  status_ = ContainerStatus::Active;
  return OpResult::success();
}

OpResult TradingContainer::upgrade(ContainerMode new_mode) {
  if (status_ != ContainerStatus::Active && status_ != ContainerStatus::WarmingUp) {
    return OpResult::fail("not running");
  }
  if (config_.mode == ContainerMode::Backtest && new_mode == ContainerMode::Paper) {
    config_.mode = ContainerMode::Paper;
    return OpResult::success();
  }
  if ((config_.mode == ContainerMode::Backtest || config_.mode == ContainerMode::Paper) &&
      new_mode == ContainerMode::Real) {
    if (capital_ != nullptr) {
      const auto committed = capital_->commit_real(config_.id);
      if (!committed.ok) {
        return committed;
      }
    }
    config_.mode = ContainerMode::Real;
    return OpResult::success();
  }
  return OpResult::fail("invalid upgrade");
}

void TradingContainer::exit() {
  if (status_ == ContainerStatus::Stopped) {
    return;
  }
  status_ = ContainerStatus::Exiting;
  if (have_bar_) {
    flatten(last_bar_.close, true);
  }
  stop();
}

void TradingContainer::force_exit(Price price) {
  if (status_ == ContainerStatus::Stopped) {
    return;
  }
  status_ = ContainerStatus::Exiting;
  flatten(price, true);
  stop();
}

void TradingContainer::stop() {
  if (status_ == ContainerStatus::Stopped) {
    return;
  }
  if (registered_) {
    risk_.unregister_container(config_.id);
    registered_ = false;
  }
  if (capital_ != nullptr) {
    capital_->release(config_.id, cash_);
  }
  status_ = ContainerStatus::Stopped;
}

void TradingContainer::on_bar(const BarEvent& bar) {
  if (bar.symbol_id != config_.symbol_id) {
    return;
  }
  last_bar_ = bar;
  have_bar_ = true;
  lib_.update(bar);
  if (strategy_ == nullptr) {
    return;
  }
  if (status_ == ContainerStatus::WarmingUp) {
    return;
  }
  if (status_ != ContainerStatus::Active) {
    return;
  }
  intents_.clear();
  strategy_->on_bar(bar, PortfolioView{position_, cash_}, intents_);
  if (!intents_.empty()) {
    SignalRecord sig{};
    sig.timestamp = bar.timestamp;
    sig.intent_count = static_cast<int>(intents_.size());
    sig.indicators_json = "{\"close_paise\":" + std::to_string(bar.close.paise()) + "}";
    signals_.push_back(std::move(sig));
  }
  for (auto& intent : intents_) {
    intent.symbol_id = config_.symbol_id;
    intent.strategy_id = config_.strategy_id;
    if (intent.price.paise() == 0) {
      intent.price = bar.close;
    }
    const auto decision = risk_.check(intent, context(),
                                      capital_ == nullptr ? nullptr : &capital_->ledger());
    if (!decision.ok()) {
      last_rejection_ = decision;
      RejectionRecord rej{};
      rej.timestamp = bar.timestamp;
      rej.rule = decision.rule != nullptr ? decision.rule : "unknown";
      rej.reason = decision.reason != nullptr ? decision.reason : "rejected";
      rejections_.push_back(std::move(rej));
      continue;
    }
    auto fill = venue_.submit(intent, bar, config_.trading_mode, cash_, position_, avg_entry_);
    if (fill) {
      fill->workbook_id = config_.workbook_id;
      fill->container_id = config_.id;
      on_fill(*fill);
      if (strategy_->should_exit()) {
        exit();
        return;
      }
    }
  }
}

void TradingContainer::on_fill(const FillEvent& fill) {
  apply_fill(fill);
  ++fills_;
  if (strategy_ != nullptr) {
    strategy_->on_fill(fill);
  }
  if (capital_ != nullptr) {
    capital_->ledger().apply_fill(fill);
  }
}

void TradingContainer::on_order_update(const OrderUpdate& update) {
  if (strategy_ != nullptr) {
    strategy_->on_order_update(update);
  }
}

void TradingContainer::on_system_event(const SystemEvent& event) {
  risk_.on_system_event(event);
  if (status_ != ContainerStatus::Active) {
    return;
  }
  if (event.type == SystemEventType::KillSwitch) {
    if (have_bar_) {
      force_exit(last_bar_.close);
    } else {
      stop();
    }
    return;
  }
  if (event.type == SystemEventType::MisSquareoffWarning &&
      config_.trading_mode == TradingMode::Mis && have_bar_) {
    flatten(last_bar_.close, true);
  }
}

void TradingContainer::apply_fill(const FillEvent& fill) {
  cash_ = Capital::from_paise(cash_.paise() + fill.net_cash_impact.paise());
  if (fill.side == Side::Buy) {
    if (position_.shares() == 0) {
      position_ = fill.filled_qty;
      avg_entry_ = fill.fill_price;
    } else {
      const auto old_n = avg_entry_.paise() * position_.shares();
      const auto add_n = fill.fill_price.paise() * fill.filled_qty.shares();
      const auto new_q = position_.shares() + fill.filled_qty.shares();
      position_ = Quantity::from_shares(new_q);
      avg_entry_ = Price::from_paise((old_n + add_n) / new_q);
    }
    return;
  }
  const auto pnl = notional(fill.fill_price, fill.filled_qty).paise() -
                   notional(avg_entry_, fill.filled_qty).paise() - fill.fees.paise();
  realized_ = Capital::from_paise(realized_.paise() + pnl);
  const auto left = position_.shares() - fill.filled_qty.shares();
  position_ = Quantity::from_shares(left < 0 ? 0 : left);
  if (position_.shares() == 0) {
    avg_entry_ = {};
  }
}

void TradingContainer::flatten(Price price, bool bypass_risk) {
  if (position_.shares() <= 0 || !have_bar_) {
    return;
  }
  BarEvent bar = last_bar_;
  bar.close = price;
  auto intent =
      make_intent(config_.strategy_id, config_.symbol_id, Side::Sell, position_, price);
  if (!bypass_risk) {
    const auto decision = risk_.check(intent, context(),
                                      capital_ == nullptr ? nullptr : &capital_->ledger());
    if (!decision.ok()) {
      last_rejection_ = decision;
      return;
    }
  }
  auto fill = venue_.submit(intent, bar, config_.trading_mode, cash_, position_, avg_entry_);
  if (fill) {
    fill->workbook_id = config_.workbook_id;
    fill->container_id = config_.id;
    on_fill(*fill);
  }
}

ContainerContext TradingContainer::context() const {
  ContainerContext ctx{};
  ctx.workbook_id = config_.workbook_id;
  ctx.container_id = config_.id;
  ctx.strategy_id = config_.strategy_id;
  ctx.symbol_id = config_.symbol_id;
  ctx.trading_mode = config_.trading_mode;
  ctx.mode = config_.mode;
  ctx.allocated = config_.mode == ContainerMode::Real ? config_.real_allocation : config_.sim_cash;
  ctx.cash = cash_;
  ctx.position = position_;
  ctx.avg_entry = avg_entry_;
  ctx.daily_pnl = realized_;
  ctx.now = last_bar_.timestamp;
  return ctx;
}

}  // namespace algocraft
