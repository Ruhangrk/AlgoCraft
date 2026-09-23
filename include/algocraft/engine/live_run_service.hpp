#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/session_date.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/market_data_feed.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace crow::websocket {
class connection;
}

namespace algocraft {

[[nodiscard]] inline bool is_live_anchor(SessionDate anchor, Timestamp now = Timestamp::now()) {
  return anchor.ok() && anchor == SessionDate::from_ist(now);
}

// Crow status WebSocket fan-out (portfolio / containers). Not market data.
class StatusWsHub {
public:
  enum class Channel { Portfolio, Containers };

  void register_conn(std::int64_t workbook_id, Channel channel, crow::websocket::connection* conn);
  void unregister_conn(crow::websocket::connection* conn);
  void push(std::int64_t workbook_id, Channel channel, std::string_view json);

private:
  struct Entry {
    std::int64_t workbook_id{0};
    Channel channel{Channel::Portfolio};
    crow::websocket::connection* conn{nullptr};
  };

  std::mutex mu_{};
  std::vector<Entry> conns_{};
};

// Async live routing: hist eval then LTPC → 1m bars → containers.
class LiveRunService {
public:
  struct Deps {
    DataSourceRegistry& data;
    StrategyRegistry& strategies;
    SymbolTable& symbols;
    ActivityRepository& activity;
    WorkbookRepository& workbooks;
    WorkbookManager& books;
    StatusWsHub* hub{nullptr};
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

private:
  struct ActiveRun;

  void run_tape(ActiveRun& active);
  void settle_and_persist(ActiveRun& active, PortfolioLedger& ledger, bool force_stopped);
  void push_status(std::int64_t wid, const PortfolioLedger& ledger,
                   const std::vector<ContainerManager::Snapshot>& snaps, std::int64_t main_paise);

  Deps deps_;
  mutable std::mutex mu_;
  // shared_ptr so ActiveRun can stay incomplete in this header (unique_ptr would not).
  std::unordered_map<std::int64_t, std::shared_ptr<ActiveRun>> active_{};
  MarketDataFeed* feed_override_{nullptr};
  int session_end_minute_{15 * 60 + 30};
  bool ignore_session_end_{false};
};

}  // namespace algocraft
