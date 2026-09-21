#include "algocraft/persistence/instrument_repository.hpp"

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

InstrumentRow read_row(sqlite3_stmt* s) {
  InstrumentRow row;
  row.ticker = column_text(s, 0);
  row.name = column_text(s, 1);
  row.isin = column_text(s, 2);
  row.exchange = column_text(s, 3);
  row.segment = column_text(s, 4);
  row.lot_size = sqlite3_column_int64(s, 5);
  row.tick_size_paise = sqlite3_column_int64(s, 6);
  row.instrument_key = column_text(s, 7);
  row.active = sqlite3_column_int(s, 8) != 0;
  row.updated_at = column_text(s, 9);
  return row;
}

void exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "unknown";
    sqlite3_free(err);
    throw std::runtime_error("sqlite exec: " + msg);
  }
}

}  // namespace

InstrumentRepository::InstrumentRepository(sqlite3* db) : db_{db} {
  if (db_ == nullptr) {
    throw std::invalid_argument("InstrumentRepository: null db");
  }
}

void InstrumentRepository::upsert(const InstrumentRow& row) {
  Stmt st(db_,
          "INSERT INTO instruments (ticker, name, isin, exchange, segment, lot_size, "
          "tick_size_paise, instrument_key, active, updated_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
          "ON CONFLICT(ticker) DO UPDATE SET "
          "  name = excluded.name, "
          "  isin = excluded.isin, "
          "  exchange = excluded.exchange, "
          "  segment = excluded.segment, "
          "  lot_size = excluded.lot_size, "
          "  tick_size_paise = excluded.tick_size_paise, "
          "  instrument_key = excluded.instrument_key, "
          "  active = excluded.active, "
          "  updated_at = excluded.updated_at");
  bind_text(st.s, 1, row.ticker);
  bind_text(st.s, 2, row.name);
  bind_text(st.s, 3, row.isin);
  bind_text(st.s, 4, row.exchange);
  bind_text(st.s, 5, row.segment);
  sqlite3_bind_int64(st.s, 6, row.lot_size);
  sqlite3_bind_int64(st.s, 7, row.tick_size_paise);
  bind_text(st.s, 8, row.instrument_key);
  sqlite3_bind_int(st.s, 9, row.active ? 1 : 0);
  if (sqlite3_step(st.s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("instrument upsert: ") + sqlite3_errmsg(db_));
  }
}

void InstrumentRepository::upsert_many(const std::vector<InstrumentRow>& rows) {
  exec(db_, "BEGIN");
  try {
    for (const auto& row : rows) {
      upsert(row);
    }
    exec(db_, "COMMIT");
  } catch (...) {
    try {
      exec(db_, "ROLLBACK");
    } catch (...) {
    }
    throw;
  }
}

void InstrumentRepository::mark_all_inactive() {
  exec(db_, "UPDATE instruments SET active = 0");
}

std::optional<InstrumentRow> InstrumentRepository::find_by_ticker(std::string_view ticker) const {
  Stmt st(db_,
          "SELECT ticker, name, isin, exchange, segment, lot_size, tick_size_paise, "
          "instrument_key, active, updated_at FROM instruments WHERE ticker = ? LIMIT 1");
  bind_text(st.s, 1, ticker);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return std::nullopt;
  }
  return read_row(st.s);
}

std::vector<InstrumentRow> InstrumentRepository::search(std::string_view query, int limit) const {
  if (limit <= 0) {
    return {};
  }
  const std::string like = std::string("%") + std::string(query) + "%";
  Stmt st(db_,
          "SELECT ticker, name, isin, exchange, segment, lot_size, tick_size_paise, "
          "instrument_key, active, updated_at FROM instruments "
          "WHERE active = 1 AND (ticker LIKE ? COLLATE NOCASE OR name LIKE ? COLLATE NOCASE) "
          "ORDER BY CASE WHEN ticker LIKE ? COLLATE NOCASE THEN 0 ELSE 1 END, ticker "
          "LIMIT ?");
  bind_text(st.s, 1, like);
  bind_text(st.s, 2, like);
  const std::string prefix = std::string(query) + "%";
  bind_text(st.s, 3, prefix);
  sqlite3_bind_int(st.s, 4, limit);
  std::vector<InstrumentRow> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    out.push_back(read_row(st.s));
  }
  return out;
}

std::int64_t InstrumentRepository::count_active() const {
  Stmt st(db_, "SELECT COUNT(*) FROM instruments WHERE active = 1");
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return 0;
  }
  return sqlite3_column_int64(st.s, 0);
}

}  // namespace algocraft
