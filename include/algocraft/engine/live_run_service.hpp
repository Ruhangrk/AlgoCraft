#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/session_date.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/engine/live_control.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/market_data_feed.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft {

class PersistenceService;

[[nodiscard]] inline bool is_live_anchor(SessionDate anchor, Timestamp now = Timestamp::now()) {
  return anchor.ok() && anchor == SessionDate::from_ist(now);
}

// One-way live status fan-out for UI (portfolio / containers). Not market data.
// Crow cannot stream chunked SSE safely on our single-threaded SQLite server, so
// the HTTP layer returns the latest snapshot as a single SSE event (EventSource
// reconnects via `retry:`). LiveRunService still pushes into this hub.
class StatusSseHub {
public:
  enum class Channel { Portfolio, Containers };

  struct Snapshot {
    std::uint64_t generation{0};
    std::string json;
  };

  void push(std::int64_t workbook_id, Channel channel, std::string_view json);
  [[nodiscard]] std::optional<Snapshot> latest(std::int64_t workbook_id, Channel channel) const;

private:
  struct Key {
    std::int64_t workbook_id{0};
    Channel channel{Channel::Portfolio};

    bool operator==(const Key& o) const {
      return workbook_id == o.workbook_id && channel == o.channel;
    }
  };

  struct KeyHash {
    std::size_t operator()(const Key& k) const {
      return std::hash<std::int64_t>{}(k.workbook_id) ^
             (static_cast<std::size_t>(k.channel) << 1);
    }
  };

  mutable std::mutex mu_{};
  std::unordered_map<Key, Snapshot, KeyHash> latest_{};
};

// Async live:
//   Feed → MarketDataRing → T0 (strategy/risk)
//   T0 → OrderOut → T3 → FillIn → T0
//   T0 → RoutingRing → T1 (router.on_bar)
//   Control/API → CommandRing → T0 (kill-switch / stop / kill container)
class LiveRunService {
public:
  static constexpr std::size_t kMarketDataCapacity = 4096;
  static constexpr std::size_t kOrderFillCapacity = 4096;
  static constexpr std::size_t kControlCapacity = 1024;
  struct Deps {
    DataSourceRegistry& data;
    StrategyRegistry& strategies;
    SymbolTable& symbols;
    ActivityRepository& activity;
    WorkbookRepository& workbooks;
    WorkbookManager& books;
    StatusSseHub* hub{nullptr};
    PersistenceService* persist{nullptr};
  };

  explicit LiveRunService(Deps deps);
  ~LiveRunService();

  LiveRunService(const LiveRunService&) = delete;
  LiveRunService& operator=(const LiveRunService&) = delete;

  struct StartResult {
    bool ok{false};
    const char* error{""};
    RunResult result{};
    bool live_started{false};
  };

  StartResult start(const RunConfig& config);
  bool stop(std::int64_t workbook_db_id);
  [[nodiscard]] bool is_running(std::int64_t workbook_db_id) const;

  void set_feed_override(MarketDataFeed* feed) { feed_override_ = feed; }
  void set_session_end_minute(int minute) { session_end_minute_ = minute; }
  void set_ignore_session_end(bool ignore) { ignore_session_end_ = ignore; }

  // Hot-path ring metrics for the active workbook (0 if not running).
  [[nodiscard]] std::uint64_t bars_processed(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t bars_dropped(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t fills_processed(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t orders_dropped(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t fills_dropped(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t commands_processed(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t routing_signals(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t commands_dropped(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::uint64_t routing_dropped(std::int64_t workbook_db_id) const;

private:
  struct ActiveRun;

  void run_tape(ActiveRun& active);
  void run_execution(ActiveRun& active);
  void run_routing(ActiveRun& active);
  void drain_bar(ActiveRun& active, const BarEvent& bar);
  void drain_fills(ActiveRun& active);
  void drain_commands(ActiveRun& active);
  void flush_execution(ActiveRun& active);
  void flush_routing(ActiveRun& active);
  void apply_pending_orders_inline(ActiveRun& active);
  void settle_and_persist(ActiveRun& active, PortfolioLedger& ledger, bool force_stopped,
                          bool flatten = true);
  void push_status(ActiveRun& active, const PortfolioLedger& ledger,
                   const std::vector<ContainerManager::Snapshot>& snaps);
  [[nodiscard]] bool enqueue_command(ActiveRun& active, const LiveCommand& cmd);

  Deps deps_;
  mutable std::mutex mu_;
  // shared_ptr so ActiveRun can stay incomplete in this header (unique_ptr would not).
  std::unordered_map<std::int64_t, std::shared_ptr<ActiveRun>> active_{};
  MarketDataFeed* feed_override_{nullptr};
  int session_end_minute_{15 * 60 + 30};
  bool ignore_session_end_{false};
};

}  // namespace algocraft
