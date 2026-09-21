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

std::vector<BacktestRow> BacktestRepository::list_for_workbook(std::int64_t workbook_id) const {
  const std::string sql =
      std::string(kSelectCols) +
      "WHERE workbook_id=? AND deleted_at IS NULL ORDER BY id ASC";
  Stmt st(db_, sql.c_str());
  sqlite3_bind_int64(st.s, 1, workbook_id);
  std::vector<BacktestRow> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    out.push_back(read_row(st.s));
  }
  return out;
}

}  // namespace algocraft
