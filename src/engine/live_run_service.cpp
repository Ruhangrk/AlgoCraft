#include "algocraft/engine/live_run_service.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/engine/live_control.hpp"
#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/execution/live_order_request.hpp"
#include "algocraft/execution/ring_execution_venue.hpp"
#include "algocraft/execution/simulated_exchange.hpp"
#include "algocraft/log/log.hpp"
#include "algocraft/persistence/persistence_service.hpp"
#include "algocraft/market_data/market_data_feed.hpp"
#include "algocraft/portfolio/capital_manager.hpp"
#include "algocraft/risk/risk_engine.hpp"
#include "algocraft/routing/routing_algo_registry.hpp"
#include "algocraft/scheduler/session_scheduler.hpp"

namespace algocraft {
namespace {

std::int64_t uuid_low(const Uuid& id) {
  std::uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val = (val << 8) | id.bytes[static_cast<std::size_t>(8 + i)];
  }
  return static_cast<std::int64_t>(val);
}

void dispatch_system_hot(const SystemEvent& ev, RiskEngine& risk, ContainerManager& containers) {
  risk.on_system_event(ev);
  containers.on_system_event(ev);
}

std::string snapshots_json(std::int64_t wid, const std::vector<ContainerManager::Snapshot>& snaps) {
  std::ostringstream out;
  out << '[';
  for (std::size_t i = 0; i < snaps.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    const auto& s = snaps[i];
    out << '{' << "\"workbook_id\":" << wid << ",\"ticker\":\"" << s.ticker << "\",\"strategy\":\""
        << s.strategy_name << "\",\"allocation_paise\":" << s.allocation.paise()
        << ",\"realized_paise\":" << s.realized.paise() << ",\"fills\":" << s.fills << '}';
  }
  out << ']';
  return out.str();
}

std::string portfolio_live_json(std::int64_t wid, std::int64_t main_paise,
                                std::int64_t available_paise, std::uint64_t bars_dropped = 0,
                                std::uint64_t orders_dropped = 0, std::uint64_t fills_dropped = 0,
                                std::uint64_t commands_dropped = 0,
                                std::uint64_t routing_dropped = 0) {
  std::ostringstream out;
  out << '{' << "\"workbook_id\":" << wid << ",\"main_capital_paise\":" << main_paise
      << ",\"available_paise\":" << available_paise << ",\"bars_dropped\":" << bars_dropped
      << ",\"orders_dropped\":" << orders_dropped << ",\"fills_dropped\":" << fills_dropped
      << ",\"commands_dropped\":" << commands_dropped << ",\"routing_dropped\":" << routing_dropped
      << '}';
  return out.str();
}

}  // namespace

struct LiveRunService::ActiveRun {
  std::int64_t workbook_db_id{0};
  RunConfig config{};
  RunResult result{};
  BorrowId borrow_id{};
  WorkbookId workbook_id{};
  std::atomic<bool> stop{false};
  std::atomic<bool> t3_stop{false};
  std::atomic<bool> t1_stop{false};
  std::thread thread{};
  std::thread t3_thread{};
  std::thread t1_thread{};
  MarketDataFeed* feed{nullptr};
  bool disconnect_feed{false};

  SpscRing<BarEvent, LiveRunService::kMarketDataCapacity> market_data{};
  SpscRing<LiveOrderRequest, LiveRunService::kOrderFillCapacity> order_out{};
  SpscRing<FillEvent, LiveRunService::kOrderFillCapacity> fill_in{};
  SpscRing<RoutingSignal, LiveRunService::kControlCapacity> routing{};
  SpscRing<LiveCommand, LiveRunService::kControlCapacity> commands{};
  std::atomic<std::uint64_t> bars_processed{0};
  std::atomic<std::uint64_t> bars_dropped{0};
  std::atomic<std::uint64_t> fills_processed{0};
  std::atomic<std::uint64_t> orders_dropped{0};
  std::atomic<std::uint64_t> fills_dropped{0};
  std::atomic<std::uint64_t> commands_processed{0};
  std::atomic<std::uint64_t> routing_signals{0};
  std::atomic<std::uint64_t> commands_dropped{0};
  std::atomic<std::uint64_t> routing_dropped{0};

  std::vector<SymbolId> stock_ids{};
  SimulatedExchange sim{};
  std::optional<RingExecutionVenue<LiveRunService::kOrderFillCapacity>> ring_venue{};
  RiskEngine risk{};
  std::optional<CapitalManager> capital{};
  std::optional<ContainerManager> containers{};
  std::unique_ptr<RoutingAlgo> router{};
  SessionScheduler scheduler{};
  PortfolioLedger* ledger{nullptr};
  std::int64_t main_paise{0};
};

LiveRunService::LiveRunService(Deps deps) : deps_(deps) {}

LiveRunService::~LiveRunService() {
  std::vector<std::int64_t> ids;
  {
    std::lock_guard lock(mu_);
    for (const auto& [id, _] : active_) {
      ids.push_back(id);
    }
  }
  for (const auto id : ids) {
    stop(id);
  }
}

bool LiveRunService::is_running(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  return active_.contains(workbook_db_id);
}

std::uint64_t LiveRunService::bars_processed(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->bars_processed.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::bars_dropped(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->bars_dropped.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::fills_processed(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->fills_processed.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::orders_dropped(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->orders_dropped.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::fills_dropped(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->fills_dropped.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::commands_processed(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->commands_processed.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::routing_signals(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->routing_signals.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::commands_dropped(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->commands_dropped.load(std::memory_order_relaxed);
}

std::uint64_t LiveRunService::routing_dropped(std::int64_t workbook_db_id) const {
  std::lock_guard lock(mu_);
  const auto it = active_.find(workbook_db_id);
  if (it == active_.end()) {
    return 0;
  }
  return it->second->routing_dropped.load(std::memory_order_relaxed);
}

bool LiveRunService::enqueue_command(ActiveRun& active, const LiveCommand& cmd) {
  if (!active.commands.try_push(cmd)) {
    active.commands_dropped.fetch_add(1, std::memory_order_relaxed);
    AC_LOG_WARN("live_command_ring_full wid={} dropped={}", active.workbook_db_id,
                active.commands_dropped.load(std::memory_order_relaxed));
    return false;
  }
  return true;
}

void LiveRunService::drain_commands(ActiveRun& active) {
  LiveCommand cmd{};
  while (active.commands.try_pop(cmd)) {
    active.commands_processed.fetch_add(1, std::memory_order_relaxed);
    switch (cmd.type) {
      case LiveCommandType::System:
        dispatch_system_hot(cmd.system, active.risk, *active.containers);
        if (cmd.system.type == SystemEventType::SessionStart ||
            cmd.system.type == SystemEventType::SessionEnd) {
          RoutingSignal signal{};
          signal.kind = cmd.system.type == SystemEventType::SessionStart
                            ? RoutingSignalKind::SessionStart
                            : RoutingSignalKind::SessionEnd;
          if (!active.routing.try_push(signal)) {
            active.routing_dropped.fetch_add(1, std::memory_order_relaxed);
          }
        }
        break;
      case LiveCommandType::StopRun:
        active.stop.store(true, std::memory_order_relaxed);
        break;
      case LiveCommandType::KillContainer:
        active.containers->kill(cmd.container_id);
        break;
    }
  }
}

void LiveRunService::drain_bar(ActiveRun& active, const BarEvent& bar) {
  for (const auto& ev : active.scheduler.on_bar(bar.timestamp)) {
    dispatch_system_hot(ev, active.risk, *active.containers);
    if (ev.type == SystemEventType::SessionStart || ev.type == SystemEventType::SessionEnd) {
      RoutingSignal signal{};
      signal.kind = ev.type == SystemEventType::SessionStart ? RoutingSignalKind::SessionStart
                                                            : RoutingSignalKind::SessionEnd;
      if (!active.routing.try_push(signal)) {
        active.routing_dropped.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
  active.containers->on_bar(bar);
  RoutingSignal signal{};
  signal.kind = RoutingSignalKind::Bar;
  signal.bar = bar;
  if (!active.routing.try_push(signal)) {
    active.routing_dropped.fetch_add(1, std::memory_order_relaxed);
    AC_LOG_WARN("live_routing_ring_full wid={} dropped={}", active.workbook_db_id,
                active.routing_dropped.load(std::memory_order_relaxed));
  }
  active.bars_processed.fetch_add(1, std::memory_order_relaxed);
}

void LiveRunService::drain_fills(ActiveRun& active) {
  FillEvent fill{};
  while (active.fill_in.try_pop(fill)) {
    active.containers->on_fill(fill);
    active.fills_processed.fetch_add(1, std::memory_order_relaxed);
  }
}

void LiveRunService::run_execution(ActiveRun& active) {
  AC_LOG_INFO("live_execution_started wid={}", active.workbook_db_id);
  auto process = [&](const LiveOrderRequest& req) {
    auto fill = active.sim.submit(req.intent, req.bar, req.mode, req.cash, req.position,
                                  req.avg_entry, req.container_id, req.workbook_id);
    if (!fill) {
      return;
    }
    fill->container_id = req.container_id;
    fill->workbook_id = req.workbook_id;
    if (!active.fill_in.try_push(*fill)) {
      active.fills_dropped.fetch_add(1, std::memory_order_relaxed);
      AC_LOG_WARN("live_fill_in_ring_full wid={} dropped={}", active.workbook_db_id,
                  active.fills_dropped.load(std::memory_order_relaxed));
    }
  };

  while (!active.t3_stop.load(std::memory_order_relaxed)) {
    LiveOrderRequest req{};
    bool got = false;
    while (active.order_out.try_pop(req)) {
      got = true;
      process(req);
    }
    if (!got) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  LiveOrderRequest req{};
  while (active.order_out.try_pop(req)) {
    process(req);
  }
  AC_LOG_INFO("live_execution_stopped wid={}", active.workbook_db_id);
}

void LiveRunService::flush_execution(ActiveRun& active) {
  active.t3_stop.store(true, std::memory_order_release);
  if (active.t3_thread.joinable()) {
    active.t3_thread.join();
  }
  drain_fills(active);
}

void LiveRunService::run_routing(ActiveRun& active) {
  AC_LOG_INFO("live_routing_started wid={}", active.workbook_db_id);
  auto handle = [&](const RoutingSignal& signal) {
    if (!active.router) {
      return;
    }
    switch (signal.kind) {
      case RoutingSignalKind::Bar:
        active.router->on_bar(signal.bar);
        break;
      case RoutingSignalKind::SessionStart:
        active.router->on_session_start();
        break;
      case RoutingSignalKind::SessionEnd:
        active.router->on_session_end();
        break;
    }
    active.routing_signals.fetch_add(1, std::memory_order_relaxed);
  };

  while (!active.t1_stop.load(std::memory_order_relaxed)) {
    RoutingSignal signal{};
    bool got = false;
    while (active.routing.try_pop(signal)) {
      got = true;
      handle(signal);
    }
    if (!got) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  RoutingSignal signal{};
  while (active.routing.try_pop(signal)) {
    handle(signal);
  }
  AC_LOG_INFO("live_routing_stopped wid={}", active.workbook_db_id);
}

void LiveRunService::flush_routing(ActiveRun& active) {
  active.t1_stop.store(true, std::memory_order_release);
  if (active.t1_thread.joinable()) {
    active.t1_thread.join();
  }
}

// Used when T3 is not running (pre-tape settle paths) so OrderOut still applies.
void LiveRunService::apply_pending_orders_inline(ActiveRun& active) {
  if (!active.containers) {
    return;
  }
  LiveOrderRequest req{};
  while (active.order_out.try_pop(req)) {
    auto fill = active.sim.submit(req.intent, req.bar, req.mode, req.cash, req.position,
                                  req.avg_entry, req.container_id, req.workbook_id);
    if (!fill) {
      continue;
    }
    fill->container_id = req.container_id;
    fill->workbook_id = req.workbook_id;
    active.containers->on_fill(*fill);
    active.fills_processed.fetch_add(1, std::memory_order_relaxed);
  }
}

void LiveRunService::push_status(ActiveRun& active, const PortfolioLedger& ledger,
                                 const std::vector<ContainerManager::Snapshot>& snaps) {
  if (deps_.hub == nullptr) {
    return;
  }
  const auto wid = active.workbook_db_id;
  deps_.hub->push(wid, StatusSseHub::Channel::Portfolio,
                  portfolio_live_json(wid, active.main_paise, ledger.available().paise(),
                                      active.bars_dropped.load(std::memory_order_relaxed),
                                      active.orders_dropped.load(std::memory_order_relaxed),
                                      active.fills_dropped.load(std::memory_order_relaxed),
                                      active.commands_dropped.load(std::memory_order_relaxed),
                                      active.routing_dropped.load(std::memory_order_relaxed)));
  deps_.hub->push(wid, StatusSseHub::Channel::Containers, snapshots_json(wid, snaps));
}

void LiveRunService::settle_and_persist(ActiveRun& active, PortfolioLedger& ledger,
                                        bool force_stopped, bool flatten) {
  if (active.router) {
    active.router->stop();
  }
  if (active.containers) {
    if (flatten) {
      active.containers->force_exit_remaining();
      apply_pending_orders_inline(active);
    }
    active.result.traded = active.containers->snapshots();
    for (auto& row : active.result.traded) {
      for (std::size_t i = 0; i < active.stock_ids.size(); ++i) {
        if (active.stock_ids[i] == row.symbol_id && i < active.config.tickers.size()) {
          row.ticker = active.config.tickers[i];
        }
      }
    }
    active.result.signals = active.containers->collect_signals();
    active.result.rejections = active.containers->collect_rejections();
  }
  active.result.fills = static_cast<int>(ledger.fills().size());
  active.result.force_stopped = force_stopped;

  const auto settlement = ledger.settlement();
  deps_.books.return_capital(active.workbook_id, active.borrow_id, settlement);
  active.result.returned = settlement;
  std::int64_t avail_paise = active.result.workbook_available_after.paise();
  if (const auto after = deps_.books.get_workbook(active.workbook_id)) {
    active.result.workbook_available_after = after->available_capital;
    avail_paise = after->available_capital.paise();
  }

  // Hot-path isolation: never SELECT/await SQLite on T0. Snapshot hub from memory only.
  push_status(active, ledger, active.result.traded);
  // After return_capital, available may differ from ledger.available(); refresh portfolio snapshot.
  if (deps_.hub != nullptr) {
    deps_.hub->push(active.workbook_db_id, StatusSseHub::Channel::Portfolio,
                    portfolio_live_json(active.workbook_db_id, active.main_paise, avail_paise,
                                        active.bars_dropped.load(std::memory_order_relaxed),
                                        active.orders_dropped.load(std::memory_order_relaxed),
                                        active.fills_dropped.load(std::memory_order_relaxed),
                                        active.commands_dropped.load(std::memory_order_relaxed),
                                        active.routing_dropped.load(std::memory_order_relaxed)));
  }

  const auto wid = active.workbook_db_id;
  const auto borrow_id = active.borrow_id;
  const auto cfg = active.config;
  const auto result = active.result;

  if (deps_.persist != nullptr) {
    const bool queued = deps_.persist->try_async([this, wid, avail_paise, borrow_id, cfg,
                                                  result](sqlite3* db) {
      (void)WorkbookRepository(db).set_available(wid, avail_paise);
      if (const auto* led = deps_.books.activity(borrow_id)) {
        ActivityRepository(db).persist_run(cfg, result, deps_.books, *led, deps_.symbols);
      }
    });
    if (!queued) {
      AC_LOG_WARN("live_persist_queue_full wid={}; falling back to run_sync", wid);
      deps_.persist->run_sync([this, wid, avail_paise, borrow_id, cfg, result](sqlite3* db) {
        (void)WorkbookRepository(db).set_available(wid, avail_paise);
        if (const auto* led = deps_.books.activity(borrow_id)) {
          ActivityRepository(db).persist_run(cfg, result, deps_.books, *led, deps_.symbols);
        }
      });
    }
  } else {
    // Tests / CLI without T2: sync write on the calling thread.
    (void)deps_.workbooks.set_available(wid, avail_paise);
    deps_.activity.persist_run(cfg, result, deps_.books, ledger, deps_.symbols);
  }
}

void LiveRunService::run_tape(ActiveRun& active) {
  if (active.feed == nullptr || !active.containers || !active.router || active.ledger == nullptr) {
    return;
  }

  active.t3_thread = std::thread([this, &active] { run_execution(active); });
  active.t1_thread = std::thread([this, &active] { run_routing(active); });

  // Producer: feed/WS (or test) thread. Consumer: this ActiveRun loop (T0).
  MinuteBarBuilder builder([&](const BarEvent& bar) {
    if (!active.market_data.try_push(bar)) {
      active.bars_dropped.fetch_add(1, std::memory_order_relaxed);
      AC_LOG_WARN("live_market_data_ring_full wid={} dropped={}", active.workbook_db_id,
                  active.bars_dropped.load(std::memory_order_relaxed));
    }
  });

  active.feed->set_handler(
      [&](SymbolId symbol_id, Price ltp, Timestamp ts) { builder.on_tick(symbol_id, ltp, ts); });

  for (const auto& snap : active.containers->snapshots()) {
    active.feed->subscribe(snap.symbol_id);
  }

  try {
    active.feed->connect();
    AC_LOG_INFO("live_feed_connected wid={} symbols={}", active.workbook_db_id,
                active.stock_ids.size());
  } catch (const std::exception& e) {
    AC_LOG_ERROR("live_feed_connect wid={} err={}", active.workbook_db_id, e.what());
    active.stop = true;
  }

  auto last_push = std::chrono::steady_clock::now();
  bool force = false;
  while (!active.stop.load(std::memory_order_relaxed)) {
    drain_commands(active);
    drain_fills(active);

    BarEvent bar{};
    bool drained = false;
    while (active.market_data.try_pop(bar)) {
      drained = true;
      drain_bar(active, bar);
      drain_fills(active);
    }

    if (!ignore_session_end_) {
      const auto now = Timestamp::now();
      if (ist_minute_of_day(now) >= session_end_minute_) {
        SystemEvent end{};
        end.type = SystemEventType::SessionEnd;
        end.timestamp = now;
        dispatch_system_hot(end, active.risk, *active.containers);
        RoutingSignal signal{};
        signal.kind = RoutingSignalKind::SessionEnd;
        (void)active.routing.try_push(signal);
        break;
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_push >= std::chrono::seconds(5)) {
      push_status(active, *active.ledger, active.containers->snapshots());
      last_push = now;
    }
    if (!drained) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  if (active.stop.load(std::memory_order_relaxed)) {
    force = true;
  }

  // Close open minute, drain bars/orders/fills/commands, then flatten remaining via rings.
  builder.flush();
  {
    BarEvent bar{};
    while (active.market_data.try_pop(bar)) {
      drain_bar(active, bar);
    }
  }
  drain_commands(active);
  drain_fills(active);
  if (auto end = active.scheduler.flush(Timestamp::now())) {
    dispatch_system_hot(*end, active.risk, *active.containers);
    if (end->type == SystemEventType::SessionEnd || end->type == SystemEventType::SessionStart) {
      RoutingSignal signal{};
      signal.kind = end->type == SystemEventType::SessionStart ? RoutingSignalKind::SessionStart
                                                              : RoutingSignalKind::SessionEnd;
      (void)active.routing.try_push(signal);
    }
  }
  active.containers->force_exit_remaining();
  flush_execution(active);
  flush_routing(active);

  AC_LOG_INFO(
      "live_tape_done wid={} bars={} bars_dropped={} fills={} orders_dropped={} fills_dropped={} "
      "cmds={} routing={} cmd_dropped={} routing_dropped={}",
      active.workbook_db_id, active.bars_processed.load(std::memory_order_relaxed),
      active.bars_dropped.load(std::memory_order_relaxed),
      active.fills_processed.load(std::memory_order_relaxed),
      active.orders_dropped.load(std::memory_order_relaxed),
      active.fills_dropped.load(std::memory_order_relaxed),
      active.commands_processed.load(std::memory_order_relaxed),
      active.routing_signals.load(std::memory_order_relaxed),
      active.commands_dropped.load(std::memory_order_relaxed),
      active.routing_dropped.load(std::memory_order_relaxed));

  settle_and_persist(active, *active.ledger, force, /*flatten=*/false);

  if (active.disconnect_feed && active.feed != nullptr) {
    active.feed->disconnect();
  }

  // Natural end: drop from map without joining this thread (detach).
  std::shared_ptr<ActiveRun> keep;
  {
    std::lock_guard lock(mu_);
    const auto it = active_.find(active.workbook_db_id);
    if (it != active_.end() && it->second.get() == &active) {
      keep = it->second;
      active_.erase(it);
    }
  }
  if (keep && keep->thread.joinable()) {
    keep->thread.detach();
  }
}

bool LiveRunService::stop(std::int64_t workbook_db_id) {
  AC_LOG_INFO("live_run_stop wid={}", workbook_db_id);
  std::shared_ptr<ActiveRun> owned;
  {
    std::lock_guard lock(mu_);
    const auto it = active_.find(workbook_db_id);
    if (it == active_.end()) {
      AC_LOG_WARN("live_run_stop_missing wid={}", workbook_db_id);
      return false;
    }
    // Control thread → CommandRing → T0 (kill-switch then stop).
    (void)enqueue_command(*it->second, LiveCommand::kill_switch());
    (void)enqueue_command(*it->second, LiveCommand::stop_run());
    it->second->stop = true;
    owned = it->second;
    active_.erase(it);
  }
  if (owned && owned->thread.joinable()) {
    owned->thread.join();
  }
  AC_LOG_INFO("live_run_stopped wid={}", workbook_db_id);
  return true;
}

LiveRunService::StartResult LiveRunService::start(const RunConfig& config) {
  StartResult out{};
  if (!config.existing_workbook_id) {
    out.error = "existing workbook required";
    return out;
  }
  if (!config.anchor_date || !is_live_anchor(*config.anchor_date)) {
    out.error = "anchor_date must be IST today for live";
    return out;
  }

  const auto wb = *config.existing_workbook_id;
  const auto wid = uuid_low(wb);

  {
    std::lock_guard lock(mu_);
    if (active_.contains(wid)) {
      out.error = "live run already active for workbook";
      return out;
    }
  }

  const auto book = deps_.books.get_workbook(wb);
  if (!book) {
    out.error = "workbook not loaded";
    return out;
  }

  auto active = std::make_shared<ActiveRun>();
  active->workbook_db_id = wid;
  active->config = config;
  active->workbook_id = wb;
  active->result.workbook_id = wb;
  active->result.workbook_available_before = book->available_capital;
  active->main_paise = book->main_capital.paise();

  Instrument inst{};
  active->stock_ids.reserve(config.tickers.size());
  for (const auto& ticker : config.tickers) {
    active->stock_ids.push_back(deps_.symbols.intern({.ticker = ticker}, inst));
  }

  const auto borrow = deps_.books.borrow_capital(wb, config.workbook_capital);
  if (!borrow.result.ok) {
    out.error = borrow.result.error;
    return out;
  }
  active->borrow_id = borrow.id;
  active->result.borrow_id = borrow.id;
  active->ledger = deps_.books.activity(borrow.id);
  if (active->ledger == nullptr) {
    out.error = "ledger missing";
    return out;
  }

  if (const auto refreshed = deps_.books.get_workbook(wb)) {
    const auto avail = refreshed->available_capital.paise();
    if (deps_.persist != nullptr) {
      deps_.persist->run_sync(
          [wid, avail](sqlite3* db) { (void)WorkbookRepository(db).set_available(wid, avail); });
    } else {
      (void)deps_.workbooks.set_available(wid, avail);
    }
  }

  active->capital.emplace(*active->ledger);
  active->ring_venue.emplace(active->order_out, &active->orders_dropped);
  active->containers.emplace(wb, *active->ring_venue, active->risk, *active->capital,
                             deps_.strategies);

  RoutingAlgoRegistry routers;
  register_all_routers(routers);
  active->router = routers.create(config.router);
  RoutingConfig rcfg{};
  rcfg.workbook_id = wb;
  rcfg.stocks = active->stock_ids;
  rcfg.strategies = config.strategies;
  rcfg.from = config.from;
  rcfg.to = config.to;
  active->router->configure(rcfg);
  active->router->start(deps_.data, deps_.strategies, *active->containers);

  active->result.evaluations = active->router->evaluations();
  for (auto& ev : active->result.evaluations) {
    for (std::size_t i = 0; i < active->stock_ids.size(); ++i) {
      if (active->stock_ids[i] == ev.symbol_id && i < config.tickers.size()) {
        ev.ticker = config.tickers[i];
      }
    }
    if (ev.selected) {
      ++active->result.selected;
    } else {
      ++active->result.skipped;
    }
  }
  active->result.real_containers = active->containers->real_count();

  if (active->containers->empty()) {
    settle_and_persist(*active, *active->ledger, false);
    out.ok = true;
    out.result = active->result;
    out.live_started = false;
    return out;
  }

  MarketDataFeed* feed = feed_override_;
  const bool using_override = feed != nullptr;
  feed_override_ = nullptr;
  if (feed == nullptr) {
    feed = deps_.data.active_provider().live_feed();
  }
  if (feed == nullptr) {
    settle_and_persist(*active, *active->ledger, true);
    out.ok = false;
    out.error = "no live feed available";
    out.result = active->result;
    return out;
  }
  active->feed = feed;
  active->disconnect_feed = !using_override;

  {
    std::lock_guard lock(mu_);
    active_[wid] = active;
  }
  active->thread = std::thread([this, active] { run_tape(*active); });

  AC_LOG_INFO("live_run_started wid={} router={} containers={}", wid, config.router,
              active->containers->real_count());
  AC_LOG_TRACE("live_run_started tickers={} strategies={}", config.tickers.size(),
               config.strategies.size());

  out.ok = true;
  out.result = active->result;
  out.live_started = true;
  return out;
}

void StatusSseHub::push(std::int64_t workbook_id, Channel channel, std::string_view json) {
  std::lock_guard lock(mu_);
  auto& slot = latest_[Key{workbook_id, channel}];
  ++slot.generation;
  slot.json.assign(json);
}

std::optional<StatusSseHub::Snapshot> StatusSseHub::latest(std::int64_t workbook_id,
                                                           Channel channel) const {
  std::lock_guard lock(mu_);
  const auto it = latest_.find(Key{workbook_id, channel});
  if (it == latest_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace algocraft
