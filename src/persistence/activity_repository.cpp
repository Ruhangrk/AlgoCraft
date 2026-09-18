#include "algocraft/persistence/activity_repository.hpp"

#include <sqlite3.h>
#include <cstring>
#include <stdexcept>
#include <string>

#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/portfolio/portfolio_ledger.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft {
namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

// Extract the u64 stored in the low 8 bytes of a Uuid (WorkbookId / UserId).
std::int64_t uuid_to_i64(const Uuid& id) {
  std::uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val = (val << 8) | id.bytes[static_cast<std::size_t>(8 + i)];
  }
  return static_cast<std::int64_t>(val);
}

std::string json_array(const std::vector<std::string>& v) {
  std::string s = "[";
  for (std::size_t i = 0; i < v.size(); ++i) {
    s += '"';
    s += v[i];
    s += '"';
    if (i + 1 < v.size()) {
      s += ',';
    }
  }
  s += ']';
  return s;
}

std::string_view container_mode_str(ContainerMode m) {
  switch (m) {
    case ContainerMode::Backtest: return "backtest";
    case ContainerMode::Paper: return "paper";
    case ContainerMode::Real: return "real";
  }
  return "unknown";
}

std::string_view workbook_event_type_str(WorkbookEventType t) {
  switch (t) {
    case WorkbookEventType::Created: return "created";
    case WorkbookEventType::CapitalAdded: return "capital_added";
    case WorkbookEventType::ActivityBorrow: return "borrow";
    case WorkbookEventType::ActivityReturn: return "return";
  }
  return "unknown";
}

// RAII statement wrapper
struct Stmt {
  sqlite3_stmt* s{nullptr};
  explicit Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("sqlite prepare: ") + sqlite3_errmsg(db));
    }
  }
  ~Stmt() { sqlite3_finalize(s); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
};

void exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "unknown";
    sqlite3_free(err);
    throw std::runtime_error(std::string("sqlite exec: ") + msg);
  }
}

void step_done(sqlite3* db, sqlite3_stmt* s) {
  const int rc = sqlite3_step(s);
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string("sqlite step: ") + sqlite3_errmsg(db));
  }
}

std::int64_t last_id(sqlite3* db) { return sqlite3_last_insert_rowid(db); }

// ── Upsert user / workbook ────────────────────────────────────────────────────

void ensure_user(sqlite3* db, std::int64_t user_db_id) {
  // Insert placeholder user; 5.6 (Auth) will replace password hash properly.
  Stmt st(db, "INSERT OR IGNORE INTO users (id, username, password_hash, role) "
              "VALUES (?, 'user_' || ?, 'unset', 'user')");
  sqlite3_bind_int64(st.s, 1, user_db_id);
  sqlite3_bind_int64(st.s, 2, user_db_id);
  step_done(db, st.s);
}

void upsert_workbook(sqlite3* db, std::int64_t wb_db_id, std::int64_t user_db_id,
                     const std::string& name, std::int64_t main_paise,
                     std::int64_t available_paise) {
  Stmt st(db,
          "INSERT INTO workbooks (id, user_id, name, main_capital_paise, available_paise) "
          "VALUES (?, ?, ?, ?, ?) "
          "ON CONFLICT(id) DO UPDATE SET "
          "  available_paise = excluded.available_paise, "
          "  main_capital_paise = excluded.main_capital_paise");
  sqlite3_bind_int64(st.s, 1, wb_db_id);
  sqlite3_bind_int64(st.s, 2, user_db_id);
  sqlite3_bind_text(st.s, 3, name.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(st.s, 4, main_paise);
  sqlite3_bind_int64(st.s, 5, available_paise);
  step_done(db, st.s);
}

void insert_capital_events(sqlite3* db, std::int64_t wb_db_id,
                            const std::vector<WorkbookEvent>& events) {
  Stmt st(db,
          "INSERT INTO workbook_capital_events (workbook_id, type, amount_paise) "
          "VALUES (?, ?, ?)");
  for (const auto& ev : events) {
    sqlite3_reset(st.s);
    sqlite3_clear_bindings(st.s);
    sqlite3_bind_int64(st.s, 1, wb_db_id);
    const auto type_str = workbook_event_type_str(ev.type);
    sqlite3_bind_text(st.s, 2, type_str.data(), static_cast<int>(type_str.size()), SQLITE_STATIC);
    sqlite3_bind_int64(st.s, 3, ev.amount.paise());
    step_done(db, st.s);
  }
}

}  // namespace

// ── ActivityRepository ────────────────────────────────────────────────────────

ActivityRepository::ActivityRepository(sqlite3* db) : db_{db} {}

std::int64_t ActivityRepository::insert_run_(std::int64_t workbook_db_id,
                                              const RunConfig& config,
                                              const RunResult& result) {
  const auto strategies_json = json_array(config.strategies);
  const auto tickers_json = json_array(config.tickers);
  Stmt st(db_,
          "INSERT INTO runs "
          "(workbook_id, router, strategies_json, tickers_json, capital_paise, "
          " selected, skipped, fills, returned_paise) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  sqlite3_bind_int64(st.s, 1, workbook_db_id);
  sqlite3_bind_text(st.s, 2, config.router.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(st.s, 3, strategies_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 4, tickers_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st.s, 5, config.workbook_capital.paise());
  sqlite3_bind_int(st.s, 6, result.selected);
  sqlite3_bind_int(st.s, 7, result.skipped);
  sqlite3_bind_int(st.s, 8, result.fills);
  sqlite3_bind_int64(st.s, 9, result.returned.paise());
  step_done(db_, st.s);
  return last_id(db_);
}

std::vector<std::pair<ContainerId, std::int64_t>> ActivityRepository::insert_containers_(
    std::int64_t run_db_id, std::int64_t workbook_db_id, const RunResult& result) {
  std::vector<std::pair<ContainerId, std::int64_t>> mapping;
  mapping.reserve(result.traded.size());

  Stmt st(db_,
          "INSERT INTO containers "
          "(run_id, workbook_id, ticker, strategy_name, mode, "
          " allocation_paise, realized_paise, fills) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

  for (const auto& snap : result.traded) {
    sqlite3_reset(st.s);
    sqlite3_clear_bindings(st.s);
    sqlite3_bind_int64(st.s, 1, run_db_id);
    sqlite3_bind_int64(st.s, 2, workbook_db_id);
    sqlite3_bind_text(st.s, 3, snap.ticker.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(st.s, 4, snap.strategy_name.c_str(), -1, SQLITE_STATIC);
    const auto mode_str = container_mode_str(snap.mode);
    sqlite3_bind_text(st.s, 5, mode_str.data(), static_cast<int>(mode_str.size()), SQLITE_STATIC);
    sqlite3_bind_int64(st.s, 6, snap.allocation.paise());
    sqlite3_bind_int64(st.s, 7, snap.realized.paise());
    sqlite3_bind_int(st.s, 8, snap.fills);
    step_done(db_, st.s);
    const auto row_id = last_id(db_);
    mapping.emplace_back(snap.id, row_id);
  }
  return mapping;
}

void ActivityRepository::insert_fills_(
    std::int64_t run_db_id, std::int64_t workbook_db_id,
    const std::vector<std::pair<ContainerId, std::int64_t>>& cmap,
    const PortfolioLedger& ledger, const SymbolTable& symbols) {
  Stmt st(db_,
          "INSERT INTO fills "
          "(workbook_id, run_id, container_id, ticker, side, qty, price_paise, "
          " fees_paise, timestamp_ns) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");

  for (const auto& fill : ledger.fills()) {
    // Look up db container_id from mapping
    std::int64_t db_container_id = 0;
    for (const auto& [cid, row_id] : cmap) {
      if (cid == fill.container_id) {
        db_container_id = row_id;
        break;
      }
    }
    if (db_container_id == 0) {
      continue;  // orphan fill (shouldn't happen in normal flow)
    }

    std::string ticker;
    if (symbols.contains(fill.symbol_id)) {
      ticker = symbols.symbol(fill.symbol_id).ticker;
    }
    const char* side_str = fill.side == Side::Buy ? "buy" : "sell";

    sqlite3_reset(st.s);
    sqlite3_clear_bindings(st.s);
    sqlite3_bind_int64(st.s, 1, workbook_db_id);
    sqlite3_bind_int64(st.s, 2, run_db_id);
    sqlite3_bind_int64(st.s, 3, db_container_id);
    sqlite3_bind_text(st.s, 4, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 5, side_str, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st.s, 6, fill.filled_qty.shares());
    sqlite3_bind_int64(st.s, 7, fill.fill_price.paise());
    sqlite3_bind_int64(st.s, 8, fill.fees.paise());
    sqlite3_bind_int64(st.s, 9, fill.timestamp.nanos());
    step_done(db_, st.s);
  }
}

void ActivityRepository::insert_signals_(
    std::int64_t run_db_id, std::int64_t workbook_db_id,
    const std::vector<std::pair<ContainerId, std::int64_t>>& cmap, const RunResult& result,
    const SymbolTable& symbols) {
  if (result.signals.empty()) {
    return;
  }
  Stmt st(db_,
          "INSERT INTO strategy_signals "
          "(workbook_id, run_id, container_id, ticker, strategy_name, intent_count, "
          " indicators_json, timestamp_ns) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  for (const auto& sig : result.signals) {
    std::int64_t db_container_id = 0;
    for (const auto& [cid, row_id] : cmap) {
      if (cid == sig.container_id) {
        db_container_id = row_id;
        break;
      }
    }
    if (db_container_id == 0) {
      continue;
    }
    std::string ticker;
    if (symbols.contains(sig.symbol_id)) {
      ticker = symbols.symbol(sig.symbol_id).ticker;
    }
    sqlite3_reset(st.s);
    sqlite3_clear_bindings(st.s);
    sqlite3_bind_int64(st.s, 1, workbook_db_id);
    sqlite3_bind_int64(st.s, 2, run_db_id);
    sqlite3_bind_int64(st.s, 3, db_container_id);
    sqlite3_bind_text(st.s, 4, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 5, sig.strategy_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st.s, 6, sig.intent_count);
    sqlite3_bind_text(st.s, 7, sig.indicators_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st.s, 8, sig.timestamp.nanos());
    step_done(db_, st.s);
  }
}

void ActivityRepository::insert_rejections_(
    std::int64_t run_db_id, std::int64_t workbook_db_id,
    const std::vector<std::pair<ContainerId, std::int64_t>>& cmap, const RunResult& result,
    const SymbolTable& symbols) {
  if (result.rejections.empty()) {
    return;
  }
  Stmt st(db_,
          "INSERT INTO risk_rejections "
          "(workbook_id, run_id, container_id, ticker, rule_name, reason, timestamp_ns) "
          "VALUES (?, ?, ?, ?, ?, ?, ?)");
  for (const auto& rej : result.rejections) {
    std::int64_t db_container_id = 0;
    for (const auto& [cid, row_id] : cmap) {
      if (cid == rej.container_id) {
        db_container_id = row_id;
        break;
      }
    }
    if (db_container_id == 0) {
      continue;
    }
    std::string ticker;
    if (symbols.contains(rej.symbol_id)) {
      ticker = symbols.symbol(rej.symbol_id).ticker;
    }
    sqlite3_reset(st.s);
    sqlite3_clear_bindings(st.s);
    sqlite3_bind_int64(st.s, 1, workbook_db_id);
    sqlite3_bind_int64(st.s, 2, run_db_id);
    sqlite3_bind_int64(st.s, 3, db_container_id);
    sqlite3_bind_text(st.s, 4, ticker.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 5, rej.rule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.s, 6, rej.reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st.s, 7, rej.timestamp.nanos());
    step_done(db_, st.s);
  }
}

void ActivityRepository::persist_run(const RunConfig& config, const RunResult& result,
                                      const WorkbookManager& books, const PortfolioLedger& ledger,
                                      const SymbolTable& symbols) {
  const auto wb_db_id = uuid_to_i64(result.workbook_id);
  const auto user_db_id = uuid_to_i64(config.user_id);

  exec(db_, "BEGIN");
  try {
    ensure_user(db_, user_db_id);

    // Workbook (post-run state)
    const auto wb_opt = books.get_workbook(result.workbook_id);
    std::int64_t main_paise = config.workbook_capital.paise();
    std::int64_t avail_paise = result.workbook_available_after.paise();
    if (wb_opt) {
      main_paise = wb_opt->main_capital.paise();
      avail_paise = wb_opt->available_capital.paise();
    }
    upsert_workbook(db_, wb_db_id, user_db_id, config.workbook_name, main_paise, avail_paise);

    // Capital events (only from this workbook)
    insert_capital_events(db_, wb_db_id, books.events(result.workbook_id));

    // Run row
    const auto run_db_id = insert_run_(wb_db_id, config, result);

    // Containers
    const auto cmap = insert_containers_(run_db_id, wb_db_id, result);

    // Fills
    insert_fills_(run_db_id, wb_db_id, cmap, ledger, symbols);

    // Signals + rejections (5.5)
    insert_signals_(run_db_id, wb_db_id, cmap, result, symbols);
    insert_rejections_(run_db_id, wb_db_id, cmap, result, symbols);

    exec(db_, "COMMIT");
  } catch (...) {
    exec(db_, "ROLLBACK");
    throw;
  }
}

// ── Read-back ─────────────────────────────────────────────────────────────────

std::vector<ActivityRepository::RunRow> ActivityRepository::list_runs(
    std::int64_t workbook_db_id) const {
  std::vector<RunRow> rows;
  Stmt st(db_,
          "SELECT id, workbook_id, router, capital_paise, selected, skipped, fills, "
          "returned_paise FROM runs WHERE workbook_id=? AND deleted_at IS NULL "
          "ORDER BY id ASC");
  sqlite3_bind_int64(st.s, 1, workbook_db_id);
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    RunRow r{};
    r.id = sqlite3_column_int64(st.s, 0);
    r.workbook_id = sqlite3_column_int64(st.s, 1);
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2))) {
      r.router = t;
    }
    r.capital_paise = sqlite3_column_int64(st.s, 3);
    r.selected = sqlite3_column_int(st.s, 4);
    r.skipped = sqlite3_column_int(st.s, 5);
    r.fills = sqlite3_column_int(st.s, 6);
    r.returned_paise = sqlite3_column_int64(st.s, 7);
    rows.push_back(r);
  }
  return rows;
}

std::vector<ActivityRepository::ContainerRow> ActivityRepository::list_containers(
    std::int64_t run_db_id) const {
  std::vector<ContainerRow> rows;
  Stmt st(db_,
          "SELECT id, run_id, ticker, strategy_name, mode, realized_paise, fills "
          "FROM containers WHERE run_id=? AND deleted_at IS NULL ORDER BY id ASC");
  sqlite3_bind_int64(st.s, 1, run_db_id);
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    ContainerRow r{};
    r.id = sqlite3_column_int64(st.s, 0);
    r.run_id = sqlite3_column_int64(st.s, 1);
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2))) r.ticker = t;
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 3)))
      r.strategy_name = t;
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 4))) r.mode = t;
    r.realized_paise = sqlite3_column_int64(st.s, 5);
    r.fills = sqlite3_column_int(st.s, 6);
    rows.push_back(r);
  }
  return rows;
}

std::vector<ActivityRepository::FillRow> ActivityRepository::list_fills(
    std::int64_t run_db_id) const {
  std::vector<FillRow> rows;
  Stmt st(db_,
          "SELECT f.id, f.container_id, f.ticker, f.side, f.qty, f.price_paise, "
          "f.fees_paise, f.timestamp_ns "
          "FROM fills f WHERE f.run_id=? ORDER BY f.timestamp_ns ASC");
  sqlite3_bind_int64(st.s, 1, run_db_id);
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    FillRow r{};
    r.id = sqlite3_column_int64(st.s, 0);
    r.container_id = sqlite3_column_int64(st.s, 1);
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 2))) r.ticker = t;
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 3))) r.side = t;
    r.qty = sqlite3_column_int64(st.s, 4);
    r.price_paise = sqlite3_column_int64(st.s, 5);
    r.fees_paise = sqlite3_column_int64(st.s, 6);
    r.timestamp_ns = sqlite3_column_int64(st.s, 7);
    rows.push_back(r);
  }
  return rows;
}

int ActivityRepository::count_signals(std::int64_t run_db_id) const {
  Stmt st(db_, "SELECT COUNT(*) FROM strategy_signals WHERE run_id=?");
  sqlite3_bind_int64(st.s, 1, run_db_id);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return 0;
  }
  return sqlite3_column_int(st.s, 0);
}

int ActivityRepository::count_rejections(std::int64_t run_db_id) const {
  Stmt st(db_, "SELECT COUNT(*) FROM risk_rejections WHERE run_id=?");
  sqlite3_bind_int64(st.s, 1, run_db_id);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return 0;
  }
  return sqlite3_column_int(st.s, 0);
}

}  // namespace algocraft
