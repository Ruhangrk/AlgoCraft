#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "algocraft/domain/ids.hpp"

struct sqlite3;

namespace algocraft {

struct RunConfig;
struct RunResult;
class WorkbookManager;
class PortfolioLedger;
class SymbolTable;

// Writes workbooks / runs / containers / fills to SQLite after a run completes.
// Must only be called on Thread 2 (persistence) or the main thread (non-hot-path).
class ActivityRepository {
public:
  explicit ActivityRepository(sqlite3* db);

  // Persist the outcome of one RunManager::execute call.
  // Safe to call multiple times (runs are independent rows; workbook is upserted).
  void persist_run(const RunConfig& config, const RunResult& result, const WorkbookManager& books,
                   const PortfolioLedger& ledger, const SymbolTable& symbols);

  // Optional list filters (empty dates / 0 limit|cursor = no bound).
  struct ListFilter {
    std::string from_date;       // YYYY-MM-DD inclusive on created_at
    std::string to_date;         // YYYY-MM-DD inclusive
    int limit{0};                // 0 = no LIMIT
    std::int64_t cursor{0};      // return id < cursor (page older when ordered DESC)
  };

  // ── Read-back helpers (for tests and API) ────────────────────────────────
  struct RunRow {
    std::int64_t id{};
    std::int64_t workbook_id{};
    std::string router;
    std::int64_t capital_paise{};
    int selected{};
    int skipped{};
    int fills{};
    std::int64_t returned_paise{};
    std::string created_at;
  };

  struct ContainerRow {
    std::int64_t id{};
    std::int64_t run_id{};
    std::int64_t workbook_id{};
    std::string ticker;
    std::string strategy_name;
    std::string mode;
    std::int64_t allocation_paise{};
    std::int64_t realized_paise{};
    int fills{};
    std::string created_at;
  };

  struct FillRow {
    std::int64_t id{};
    std::int64_t container_id{};
    std::string ticker;
    std::string side;
    std::int64_t qty{};
    std::int64_t price_paise{};
    std::int64_t fees_paise{};
    std::int64_t timestamp_ns{};
  };

  struct SignalRow {
    std::int64_t id{};
    std::int64_t container_id{};
    std::string ticker;
    std::string strategy_name;
    int intent_count{};
    std::string indicators_json;
    std::int64_t timestamp_ns{};
  };

  struct RejectionRow {
    std::int64_t id{};
    std::int64_t container_id{};
    std::string ticker;
    std::string rule_name;
    std::string reason;
    std::int64_t timestamp_ns{};
  };

  struct RoutingRow {
    std::int64_t id{};
    std::string ticker;
    std::string strategy_name;
    std::string decision;
    std::int64_t score_paise{};
    std::string reason;
    std::int64_t timestamp_ns{};
  };

  struct LifecycleRow {
    std::int64_t id{};
    std::int64_t container_id{};  // 0 if run-level
    std::string ticker;
    std::string event_type;
    std::string detail;
    std::int64_t timestamp_ns{};
  };

  [[nodiscard]] std::optional<RunRow> find_run(std::int64_t workbook_db_id,
                                               std::int64_t run_id) const;

  [[nodiscard]] std::vector<RunRow> list_runs(std::int64_t workbook_db_id) const {
    return list_runs(workbook_db_id, ListFilter{});
  }
  [[nodiscard]] std::vector<RunRow> list_runs(std::int64_t workbook_db_id,
                                              const ListFilter& filter) const;
  // Soft-delete: sets deleted_at. Returns false if missing/already deleted.
  [[nodiscard]] bool soft_delete_run(std::int64_t workbook_db_id, std::int64_t run_id);

  [[nodiscard]] std::vector<ContainerRow> list_containers(std::int64_t run_db_id) const;
  // workbook-scoped lookup (404 if wrong workbook or soft-deleted).
  [[nodiscard]] std::optional<ContainerRow> find_container(std::int64_t workbook_db_id,
                                                           std::int64_t container_id) const;
  [[nodiscard]] std::vector<FillRow> list_fills(std::int64_t run_db_id) const;
  [[nodiscard]] std::vector<SignalRow> list_signals(std::int64_t run_db_id) const;
  [[nodiscard]] std::vector<RejectionRow> list_rejections(std::int64_t run_db_id) const;
  [[nodiscard]] std::vector<RoutingRow> list_routing(std::int64_t run_db_id) const;
  [[nodiscard]] std::vector<LifecycleRow> list_lifecycle(std::int64_t run_db_id) const;
  [[nodiscard]] int count_signals(std::int64_t run_db_id) const;
  [[nodiscard]] int count_rejections(std::int64_t run_db_id) const;

private:
  sqlite3* db_{nullptr};

  // Returns the db row id (autoincrement) of the run just inserted.
  std::int64_t insert_run_(std::int64_t workbook_db_id, const RunConfig& config,
                            const RunResult& result);

  // Returns ContainerId -> db_row_id mapping for the containers just inserted.
  std::vector<std::pair<ContainerId, std::int64_t>> insert_containers_(
      std::int64_t run_db_id, std::int64_t workbook_db_id, const RunResult& result);

  void insert_fills_(std::int64_t run_db_id, std::int64_t workbook_db_id,
                     const std::vector<std::pair<ContainerId, std::int64_t>>& cmap,
                     const PortfolioLedger& ledger, const SymbolTable& symbols);

  void insert_signals_(std::int64_t run_db_id, std::int64_t workbook_db_id,
                       const std::vector<std::pair<ContainerId, std::int64_t>>& cmap,
                       const RunResult& result, const SymbolTable& symbols);

  void insert_rejections_(std::int64_t run_db_id, std::int64_t workbook_db_id,
                          const std::vector<std::pair<ContainerId, std::int64_t>>& cmap,
                          const RunResult& result, const SymbolTable& symbols);

  void insert_routing_(std::int64_t run_db_id, std::int64_t workbook_db_id,
                       const RunResult& result);
  void insert_lifecycle_(std::int64_t run_db_id, std::int64_t workbook_db_id,
                         const std::vector<std::pair<ContainerId, std::int64_t>>& cmap,
                         const RunResult& result);
};

}  // namespace algocraft
