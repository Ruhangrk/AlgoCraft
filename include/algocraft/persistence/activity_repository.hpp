#pragma once

#include <cstdint>
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
  };

  struct ContainerRow {
    std::int64_t id{};
    std::int64_t run_id{};
    std::string ticker;
    std::string strategy_name;
    std::string mode;
    std::int64_t realized_paise{};
    int fills{};
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

  [[nodiscard]] std::vector<RunRow> list_runs(std::int64_t workbook_db_id) const;
  [[nodiscard]] std::vector<ContainerRow> list_containers(std::int64_t run_db_id) const;
  [[nodiscard]] std::vector<FillRow> list_fills(std::int64_t run_db_id) const;
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
};

}  // namespace algocraft
