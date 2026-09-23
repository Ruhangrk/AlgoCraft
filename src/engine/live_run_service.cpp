#include "algocraft/engine/live_run_service.hpp"

#include <crow.h>

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
#include "algocraft/execution/simulated_exchange.hpp"
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

void dispatch_system(const SystemEvent& ev, RiskEngine& risk, ContainerManager& containers,
                     RoutingAlgo& router) {
  risk.on_system_event(ev);
  if (ev.type == SystemEventType::SessionStart) {
    router.on_session_start();
  } else if (ev.type == SystemEventType::SessionEnd) {
    router.on_session_end();
  }
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
                                std::int64_t available_paise) {
  std::ostringstream out;
  out << '{' << "\"workbook_id\":" << wid << ",\"main_capital_paise\":" << main_paise
      << ",\"available_paise\":" << available_paise << '}';
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
  std::thread thread{};
  MarketDataFeed* feed{nullptr};
  bool disconnect_feed{false};

  std::vector<SymbolId> stock_ids{};
  SimulatedExchange venue{};
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

void LiveRunService::push_status(std::int64_t wid, const PortfolioLedger& ledger,
                                 const std::vector<ContainerManager::Snapshot>& snaps,
                                 std::int64_t main_paise) {
  if (deps_.hub == nullptr) {
    return;
  }
  deps_.hub->push(wid, StatusWsHub::Channel::Portfolio,
                  portfolio_live_json(wid, main_paise, ledger.available().paise()));
  deps_.hub->push(wid, StatusWsHub::Channel::Containers, snapshots_json(wid, snaps));
}

void LiveRunService::settle_and_persist(ActiveRun& active, PortfolioLedger& ledger,
                                        bool force_stopped) {
  if (active.router) {
    active.router->stop();
  }
  if (active.containers) {
    active.containers->force_exit_remaining();
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
  if (const auto after = deps_.books.get_workbook(active.workbook_id)) {
    active.result.workbook_available_after = after->available_capital;
    (void)deps_.workbooks.set_available(active.workbook_db_id, after->available_capital.paise());
  }

  deps_.activity.persist_run(active.config, active.result, deps_.books, ledger, deps_.symbols);

  push_status(active.workbook_db_id, ledger, active.result.traded, active.main_paise);
  if (deps_.hub != nullptr) {
    if (const auto row = deps_.workbooks.find(active.workbook_db_id)) {
      deps_.hub->push(active.workbook_db_id, StatusWsHub::Channel::Portfolio,
                      portfolio_live_json(active.workbook_db_id, row->main_capital_paise,
                                          row->available_paise));
    }
  }
}

void LiveRunService::run_tape(ActiveRun& active) {
  if (active.feed == nullptr || !active.containers || !active.router || active.ledger == nullptr) {
    return;
  }

  MinuteBarBuilder builder([&](const BarEvent& bar) {
    for (const auto& ev : active.scheduler.on_bar(bar.timestamp)) {
      dispatch_system(ev, active.risk, *active.containers, *active.router);
    }
    active.containers->on_bar(bar);
    active.router->on_bar(bar);
  });

  active.feed->set_handler(
      [&](SymbolId symbol_id, Price ltp, Timestamp ts) { builder.on_tick(symbol_id, ltp, ts); });

  for (const auto& snap : active.containers->snapshots()) {
    active.feed->subscribe(snap.symbol_id);
  }

  try {
    active.feed->connect();
  } catch (const std::exception& e) {
    spdlog::error("live feed connect: {}", e.what());
    active.stop = true;
  }

  auto last_push = std::chrono::steady_clock::now();
  bool force = false;
  while (!active.stop.load()) {
    if (!ignore_session_end_) {
      const auto now = Timestamp::now();
      if (ist_minute_of_day(now) >= session_end_minute_) {
        break;
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_push >= std::chrono::seconds(5)) {
      push_status(active.workbook_db_id, *active.ledger, active.containers->snapshots(),
                  active.main_paise);
      last_push = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (active.stop.load()) {
    force = true;
  }

  builder.flush();
  if (auto end = active.scheduler.flush(Timestamp::now())) {
    dispatch_system(*end, active.risk, *active.containers, *active.router);
  }

  settle_and_persist(active, *active.ledger, force);

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
  std::shared_ptr<ActiveRun> owned;
  {
    std::lock_guard lock(mu_);
    const auto it = active_.find(workbook_db_id);
    if (it == active_.end()) {
      return false;
    }
    it->second->stop = true;
    owned = it->second;
    active_.erase(it);
  }
  if (owned && owned->thread.joinable()) {
    owned->thread.join();
  }
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
    (void)deps_.workbooks.set_available(wid, refreshed->available_capital.paise());
  }

  active->capital.emplace(*active->ledger);
  active->containers.emplace(wb, active->venue, active->risk, *active->capital, deps_.strategies);

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

  out.ok = true;
  out.result = active->result;
  out.live_started = true;
  return out;
}

void StatusWsHub::register_conn(std::int64_t workbook_id, Channel channel,
                                crow::websocket::connection* conn) {
  if (conn == nullptr) {
    return;
  }
  std::lock_guard lock(mu_);
  conns_.push_back(Entry{workbook_id, channel, conn});
}

void StatusWsHub::unregister_conn(crow::websocket::connection* conn) {
  if (conn == nullptr) {
    return;
  }
  std::lock_guard lock(mu_);
  conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                              [conn](const Entry& e) { return e.conn == conn; }),
               conns_.end());
}

void StatusWsHub::push(std::int64_t workbook_id, Channel channel, std::string_view json) {
  std::vector<crow::websocket::connection*> targets;
  {
    std::lock_guard lock(mu_);
    for (const auto& e : conns_) {
      if (e.workbook_id == workbook_id && e.channel == channel && e.conn != nullptr) {
        targets.push_back(e.conn);
      }
    }
  }
  const std::string payload{json};
  for (auto* conn : targets) {
    try {
      conn->send_text(payload);
    } catch (...) {
    }
  }
}

}  // namespace algocraft
