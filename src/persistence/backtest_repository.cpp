#include "algocraft/persistence/backtest_repository.hpp"

#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace algocraft {
namespace {

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

void bind_text(sqlite3_stmt* s, int idx, std::string_view value) {
  sqlite3_bind_text(s, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::string column_text(sqlite3_stmt* s, int idx) {
  if (sqlite3_column_type(s, idx) == SQLITE_NULL) {
    return {};
  }
  const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(s, idx));
  return t ? std::string{t} : std::string{};
}

BacktestRow read_row(sqlite3_stmt* s) {
  BacktestRow row;
  row.id = sqlite3_column_int64(s, 0);
  row.workbook_id = sqlite3_column_int64(s, 1);
  row.strategy_name = column_text(s, 2);
  row.ticker = column_text(s, 3);
  row.capital_paise = sqlite3_column_int64(s, 4);
  row.from_ns = sqlite3_column_int64(s, 5);
  row.to_ns = sqlite3_column_int64(s, 6);
  row.ending_equity_paise = sqlite3_column_int64(s, 7);
  row.pnl_paise = sqlite3_column_int64(s, 8);
  row.fees_paise = sqlite3_column_int64(s, 9);
  row.return_pct_bp = sqlite3_column_int64(s, 10);
  row.max_drawdown_paise = sqlite3_column_int64(s, 11);
  row.fills = sqlite3_column_int(s, 12);
  row.bars = sqlite3_column_int(s, 13);
  row.status = column_text(s, 14);
  row.created_at = column_text(s, 15);
  return row;
}

constexpr const char* kSelectCols =
    "SELECT id, workbook_id, strategy_name, ticker, capital_paise, from_ns, to_ns, "
    "ending_equity_paise, pnl_paise, fees_paise, return_pct_bp, max_drawdown_paise, "
    "fills, bars, status, created_at FROM backtests ";

}  // namespace

BacktestRepository::BacktestRepository(sqlite3* db) : db_{db} {
  if (db_ == nullptr) {
    throw std::invalid_argument("BacktestRepository: null db");
  }
}

std::int64_t BacktestRepository::insert(const BacktestRow& row) {
  Stmt st(db_,
          "INSERT INTO backtests (workbook_id, strategy_name, ticker, capital_paise, from_ns, "
          "to_ns, ending_equity_paise, pnl_paise, fees_paise, return_pct_bp, max_drawdown_paise, "
          "fills, bars, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  sqlite3_bind_int64(st.s, 1, row.workbook_id);
  bind_text(st.s, 2, row.strategy_name);
  bind_text(st.s, 3, row.ticker);
  sqlite3_bind_int64(st.s, 4, row.capital_paise);
  sqlite3_bind_int64(st.s, 5, row.from_ns);
  sqlite3_bind_int64(st.s, 6, row.to_ns);
  sqlite3_bind_int64(st.s, 7, row.ending_equity_paise);
  sqlite3_bind_int64(st.s, 8, row.pnl_paise);
  sqlite3_bind_int64(st.s, 9, row.fees_paise);
  sqlite3_bind_int64(st.s, 10, row.return_pct_bp);
  sqlite3_bind_int64(st.s, 11, row.max_drawdown_paise);
  sqlite3_bind_int(st.s, 12, row.fills);
  sqlite3_bind_int(st.s, 13, row.bars);
  bind_text(st.s, 14, row.status.empty() ? "completed" : row.status);
  if (sqlite3_step(st.s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("backtest insert: ") + sqlite3_errmsg(db_));
  }
  return sqlite3_last_insert_rowid(db_);
}

std::optional<BacktestRow> BacktestRepository::find(std::int64_t workbook_id,
                                                    std::int64_t backtest_id) const {
  const std::string sql = std::string(kSelectCols) +
                          "WHERE workbook_id=? AND id=? AND deleted_at IS NULL LIMIT 1";
  Stmt st(db_, sql.c_str());
  sqlite3_bind_int64(st.s, 1, workbook_id);
  sqlite3_bind_int64(st.s, 2, backtest_id);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return std::nullopt;
  }
  return read_row(st.s);
}

std::vector<BacktestRow> BacktestRepository::list_for_workbook(
    std::int64_t workbook_id, const BacktestListFilter& filter) const {
  std::string sql = std::string(kSelectCols) + "WHERE workbook_id=? AND deleted_at IS NULL";
  if (!filter.from_date.empty()) {
    sql += " AND date(created_at) >= date(?)";
  }
  if (!filter.to_date.empty()) {
    sql += " AND date(created_at) <= date(?)";
  }
  if (filter.cursor > 0) {
    sql += " AND id < ?";
  }
  sql += " ORDER BY id DESC";
  if (filter.limit > 0) {
    sql += " LIMIT ?";
  }

  Stmt st(db_, sql.c_str());
  int idx = 1;
  sqlite3_bind_int64(st.s, idx++, workbook_id);
  if (!filter.from_date.empty()) {
    bind_text(st.s, idx++, filter.from_date);
  }
  if (!filter.to_date.empty()) {
    bind_text(st.s, idx++, filter.to_date);
  }
  if (filter.cursor > 0) {
    sqlite3_bind_int64(st.s, idx++, filter.cursor);
  }
  if (filter.limit > 0) {
    sqlite3_bind_int(st.s, idx++, filter.limit);
  }
  std::vector<BacktestRow> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    out.push_back(read_row(st.s));
  }
  return out;
}

bool BacktestRepository::soft_delete(std::int64_t workbook_id, std::int64_t backtest_id) {
  Stmt st(db_,
          "UPDATE backtests SET deleted_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
          "WHERE workbook_id=? AND id=? AND deleted_at IS NULL");
  sqlite3_bind_int64(st.s, 1, workbook_id);
  sqlite3_bind_int64(st.s, 2, backtest_id);
  if (sqlite3_step(st.s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("backtest soft_delete: ") + sqlite3_errmsg(db_));
  }
  return sqlite3_changes(db_) > 0;
}

void BacktestRepository::insert_events(std::int64_t workbook_id, std::int64_t backtest_id,
                                       const BacktestEventBatch& events) {
  for (const auto& s : events.signals) {
    Stmt st(db_,
            "INSERT INTO backtest_signals (workbook_id, backtest_id, ticker, strategy_name, "
            "intent_count, indicators_json, timestamp_ns) VALUES (?, ?, ?, ?, ?, ?, ?)");
    sqlite3_bind_int64(st.s, 1, workbook_id);
    sqlite3_bind_int64(st.s, 2, backtest_id);
    bind_text(st.s, 3, s.ticker.empty() ? "" : s.ticker);
    bind_text(st.s, 4, s.strategy_name);
    sqlite3_bind_int(st.s, 5, s.intent_count);
    bind_text(st.s, 6, s.indicators_json.empty() ? "{}" : s.indicators_json);
    sqlite3_bind_int64(st.s, 7, s.timestamp_ns);
    if (sqlite3_step(st.s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("backtest_signals insert: ") + sqlite3_errmsg(db_));
    }
  }
  for (const auto& r : events.rejections) {
    Stmt st(db_,
            "INSERT INTO backtest_rejections (workbook_id, backtest_id, ticker, rule_name, "
            "reason, timestamp_ns) VALUES (?, ?, ?, ?, ?, ?)");
    sqlite3_bind_int64(st.s, 1, workbook_id);
    sqlite3_bind_int64(st.s, 2, backtest_id);
    bind_text(st.s, 3, r.ticker);
    bind_text(st.s, 4, r.rule_name);
    bind_text(st.s, 5, r.reason);
    sqlite3_bind_int64(st.s, 6, r.timestamp_ns);
    if (sqlite3_step(st.s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("backtest_rejections insert: ") + sqlite3_errmsg(db_));
    }
  }
  for (const auto& f : events.fills) {
    Stmt st(db_,
            "INSERT INTO backtest_fills (workbook_id, backtest_id, ticker, side, qty, "
            "price_paise, fees_paise, timestamp_ns) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    sqlite3_bind_int64(st.s, 1, workbook_id);
    sqlite3_bind_int64(st.s, 2, backtest_id);
    bind_text(st.s, 3, f.ticker);
    bind_text(st.s, 4, f.side);
    sqlite3_bind_int64(st.s, 5, f.qty);
    sqlite3_bind_int64(st.s, 6, f.price_paise);
    sqlite3_bind_int64(st.s, 7, f.fees_paise);
    sqlite3_bind_int64(st.s, 8, f.timestamp_ns);
    if (sqlite3_step(st.s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("backtest_fills insert: ") + sqlite3_errmsg(db_));
    }
  }
}

std::vector<BacktestSignalRow> BacktestRepository::list_signals(std::int64_t backtest_id) const {
  Stmt st(db_,
          "SELECT id, ticker, strategy_name, intent_count, indicators_json, timestamp_ns "
          "FROM backtest_signals WHERE backtest_id=? ORDER BY timestamp_ns ASC, id ASC");
  sqlite3_bind_int64(st.s, 1, backtest_id);
  std::vector<BacktestSignalRow> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    BacktestSignalRow row;
    row.id = sqlite3_column_int64(st.s, 0);
    row.ticker = column_text(st.s, 1);
    row.strategy_name = column_text(st.s, 2);
    row.intent_count = sqlite3_column_int(st.s, 3);
    row.indicators_json = column_text(st.s, 4);
    row.timestamp_ns = sqlite3_column_int64(st.s, 5);
    out.push_back(std::move(row));
  }
  return out;
}

std::vector<BacktestRejectionRow> BacktestRepository::list_rejections(
    std::int64_t backtest_id) const {
  Stmt st(db_,
          "SELECT id, ticker, rule_name, reason, timestamp_ns "
          "FROM backtest_rejections WHERE backtest_id=? ORDER BY timestamp_ns ASC, id ASC");
  sqlite3_bind_int64(st.s, 1, backtest_id);
  std::vector<BacktestRejectionRow> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    BacktestRejectionRow row;
    row.id = sqlite3_column_int64(st.s, 0);
    row.ticker = column_text(st.s, 1);
    row.rule_name = column_text(st.s, 2);
    row.reason = column_text(st.s, 3);
    row.timestamp_ns = sqlite3_column_int64(st.s, 4);
    out.push_back(std::move(row));
  }
  return out;
}

std::vector<BacktestFillRow> BacktestRepository::list_fills(std::int64_t backtest_id) const {
  Stmt st(db_,
          "SELECT id, ticker, side, qty, price_paise, fees_paise, timestamp_ns "
          "FROM backtest_fills WHERE backtest_id=? ORDER BY timestamp_ns ASC, id ASC");
  sqlite3_bind_int64(st.s, 1, backtest_id);
  std::vector<BacktestFillRow> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    BacktestFillRow row;
    row.id = sqlite3_column_int64(st.s, 0);
    row.ticker = column_text(st.s, 1);
    row.side = column_text(st.s, 2);
    row.qty = sqlite3_column_int64(st.s, 3);
    row.price_paise = sqlite3_column_int64(st.s, 4);
    row.fees_paise = sqlite3_column_int64(st.s, 5);
    row.timestamp_ns = sqlite3_column_int64(st.s, 6);
    out.push_back(std::move(row));
  }
  return out;
}

}  // namespace algocraft
