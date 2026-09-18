#include "algocraft/persistence/coverage_repository.hpp"

#include <stdexcept>
#include <utility>

#include "sqlite3.h"

namespace algocraft {
namespace {

struct Stmt {
  sqlite3_stmt* s{nullptr};
  Stmt() = default;
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  ~Stmt() { sqlite3_finalize(s); }
};

void bind_text(sqlite3_stmt* stmt, int idx, std::string_view value) {
  sqlite3_bind_text(stmt, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void bind_optional_date(sqlite3_stmt* stmt, int idx, const std::optional<SessionDate>& date) {
  if (!date || !date->ok()) {
    sqlite3_bind_null(stmt, idx);
    return;
  }
  const auto iso = date->iso();
  bind_text(stmt, idx, iso);
}

void bind_optional_text(sqlite3_stmt* stmt, int idx, const std::optional<std::string>& value) {
  if (!value) {
    sqlite3_bind_null(stmt, idx);
    return;
  }
  bind_text(stmt, idx, *value);
}

std::optional<std::string> column_text(sqlite3_stmt* stmt, int idx) {
  if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  const auto* text = sqlite3_column_text(stmt, idx);
  if (text == nullptr) {
    return std::nullopt;
  }
  return std::string{reinterpret_cast<const char*>(text)};
}

std::optional<SessionDate> column_date(sqlite3_stmt* stmt, int idx) {
  const auto text = column_text(stmt, idx);
  if (!text) {
    return std::nullopt;
  }
  return SessionDate::from_iso(*text);
}

}  // namespace

CoverageRepository::CoverageRepository(sqlite3* db) : db_{db} {
  if (db_ == nullptr) {
    throw std::invalid_argument("coverage repository requires sqlite");
  }
}

std::optional<CoverageRow> CoverageRepository::get(std::string_view ticker,
                                                   std::string_view resolution) const {
  Stmt stmt;
  const int rc = sqlite3_prepare_v2(
      db_,
      "SELECT ticker, resolution, first_date, last_date, live_date, last_fetched_at, source, "
      "sessions FROM symbol_data_coverage WHERE ticker = ? AND resolution = ? LIMIT 1;",
      -1, &stmt.s, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("coverage get prepare: ") + sqlite3_errmsg(db_));
  }
  bind_text(stmt.s, 1, ticker);
  bind_text(stmt.s, 2, resolution);
  const int step = sqlite3_step(stmt.s);
  if (step == SQLITE_DONE) {
    return std::nullopt;
  }
  if (step != SQLITE_ROW) {
    throw std::runtime_error(std::string("coverage get: ") + sqlite3_errmsg(db_));
  }

  CoverageRow row{};
  row.ticker = column_text(stmt.s, 0).value_or("");
  row.resolution = column_text(stmt.s, 1).value_or("");
  row.first_date = column_date(stmt.s, 2);
  row.last_date = column_date(stmt.s, 3);
  row.live_date = column_date(stmt.s, 4);
  row.last_fetched_at = column_text(stmt.s, 5);
  row.source = column_text(stmt.s, 6).value_or("");
  row.sessions = sqlite3_column_int(stmt.s, 7);
  return row;
}

void CoverageRepository::upsert(const CoverageRow& row) {
  Stmt stmt;
  const int rc = sqlite3_prepare_v2(
      db_,
      "INSERT INTO symbol_data_coverage (ticker, resolution, first_date, last_date, live_date, "
      "last_fetched_at, source, sessions) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(ticker, resolution) DO UPDATE SET "
      "first_date=excluded.first_date, last_date=excluded.last_date, "
      "live_date=excluded.live_date, last_fetched_at=excluded.last_fetched_at, "
      "source=excluded.source, sessions=excluded.sessions;",
      -1, &stmt.s, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("coverage upsert prepare: ") + sqlite3_errmsg(db_));
  }
  bind_text(stmt.s, 1, row.ticker);
  bind_text(stmt.s, 2, row.resolution);
  bind_optional_date(stmt.s, 3, row.first_date);
  bind_optional_date(stmt.s, 4, row.last_date);
  bind_optional_date(stmt.s, 5, row.live_date);
  bind_optional_text(stmt.s, 6, row.last_fetched_at);
  bind_text(stmt.s, 7, row.source);
  sqlite3_bind_int(stmt.s, 8, row.sessions);
  if (sqlite3_step(stmt.s) != SQLITE_DONE) {
    throw std::runtime_error(std::string("coverage upsert: ") + sqlite3_errmsg(db_));
  }
}

}  // namespace algocraft
