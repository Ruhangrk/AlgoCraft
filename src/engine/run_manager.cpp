#include "algocraft/engine/run_manager.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/execution/simulated_exchange.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/portfolio/capital_manager.hpp"
#include "algocraft/risk/risk_engine.hpp"
#include "algocraft/routing/routing_algo_registry.hpp"
#include "algocraft/scheduler/session_scheduler.hpp"

namespace algocraft {
namespace {

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

void settle(RunResult& out, WorkbookManager& books, WorkbookId wb, BorrowId borrow,
            PortfolioLedger& ledger) {
  const auto settlement = ledger.settlement();
  books.return_capital(wb, borrow, settlement);
  out.returned = settlement;
  const auto book = books.get_workbook(wb);
  if (book) {
    out.workbook_available_after = book->available_capital;
  }
}

}  // namespace

RunResult RunManager::execute(const RunConfig& config, DataSourceRegistry& data,
                              StrategyRegistry& strategies, WorkbookManager& books,
                              SymbolTable& symbols) {
  RunResult out{};
  const Instrument inst{};
  std::vector<SymbolId> stock_ids;
  stock_ids.reserve(config.tickers.size());
  for (const auto& ticker : config.tickers) {
    stock_ids.push_back(symbols.intern({.ticker = ticker}, inst));
  }

  const auto wb = books.create(config.user_id, config.workbook_name, config.workbook_capital);
  out.workbook_id = wb;
  out.workbook_available_before = config.workbook_capital;
  const auto borrow = books.borrow_capital(wb, config.workbook_capital);
  if (!borrow.result.ok) {
    return out;
  }
  out.borrow_id = borrow.id;
  auto* ledger = books.activity(borrow.id);
  if (ledger == nullptr) {
    return out;
  }
  CapitalManager capital{*ledger};
  SimulatedExchange venue;
  RiskEngine risk;
  SessionScheduler scheduler;
  ContainerManager containers{wb, venue, risk, capital, strategies};

  RoutingAlgoRegistry routers;
  register_all_routers(routers);
  auto router = routers.create(config.router);
  RoutingConfig rcfg{};
  rcfg.workbook_id = wb;
  rcfg.stocks = stock_ids;
  rcfg.strategies = config.strategies;
  rcfg.from = config.from;
  rcfg.to = config.to;
  router->configure(rcfg);
  router->start(data, strategies, containers);

  out.evaluations = router->evaluations();
  for (auto& ev : out.evaluations) {
    for (std::size_t i = 0; i < stock_ids.size(); ++i) {
      if (stock_ids[i] == ev.symbol_id && i < config.tickers.size()) {
        ev.ticker = config.tickers[i];
      }
    }
    if (ev.selected) {
      ++out.selected;
    } else {
      ++out.skipped;
    }
  }
  out.real_containers = containers.real_count();

  if (containers.empty()) {
    settle(out, books, wb, borrow.id, *ledger);
    return out;
  }

  const auto tape_from = config.trade_from.nanos() != 0 ? config.trade_from : config.from;
  const auto tape_to = config.trade_to.nanos() != 0 ? config.trade_to : config.to;

  std::vector<BarEvent> tape;
  for (const auto symbol_id : stock_ids) {
    auto bars = data.active_provider().historical_loader().load_bars(
        symbol_id, tape_from, tape_to, BarResolution::OneMin);
    tape.insert(tape.end(), bars.begin(), bars.end());
  }
  std::sort(tape.begin(), tape.end(), [](const BarEvent& a, const BarEvent& b) {
    if (a.timestamp != b.timestamp) {
      return a.timestamp < b.timestamp;
    }
    return a.symbol_id < b.symbol_id;
  });

  SpscRing<BarEvent, 1024> ring;
  std::size_t next = 0;
  std::size_t processed = 0;
  Timestamp last_ts{};
  while (processed < tape.size()) {
    if (config.max_replay_bars > 0 && processed >= config.max_replay_bars) {
      out.force_stopped = true;
      break;
    }
    while (next < tape.size() && ring.try_push(tape[next])) {
      ++next;
    }
    BarEvent bar{};
    if (!ring.try_pop(bar)) {
      break;
    }
    ++processed;
    last_ts = bar.timestamp;
    for (const auto& ev : scheduler.on_bar(bar.timestamp)) {
      dispatch_system(ev, risk, containers, *router);
    }
    containers.on_bar(bar);
    router->on_bar(bar);
  }

  if (out.force_stopped) {
    risk.set_kill_switch(true);
    SystemEvent kill{};
    kill.type = SystemEventType::KillSwitch;
    kill.timestamp = last_ts;
    dispatch_system(kill, risk, containers, *router);
  } else if (auto end = scheduler.flush(last_ts)) {
    dispatch_system(*end, risk, containers, *router);
  }

  router->stop();
  containers.force_exit_remaining();

  out.traded = containers.snapshots();
  for (auto& row : out.traded) {
    for (std::size_t i = 0; i < stock_ids.size(); ++i) {
      if (stock_ids[i] == row.symbol_id && i < config.tickers.size()) {
        row.ticker = config.tickers[i];
      }
    }
  }
  out.fills = static_cast<int>(ledger->fills().size());
  settle(out, books, wb, borrow.id, *ledger);
  return out;
}

}  // namespace algocraft
