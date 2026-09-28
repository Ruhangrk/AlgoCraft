#include "algocraft/persistence/strategy_catalog_repository.hpp"

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

void step_done(sqlite3* db, sqlite3_stmt* s, const char* what) {
  const int rc = sqlite3_step(s);
  if (rc != SQLITE_DONE) {
    throw std::runtime_error(std::string(what) + ": " + sqlite3_errmsg(db) +
                             " (rc=" + std::to_string(rc) + ")");
  }
}

StrategyCatalogRepository::Row read_row(sqlite3_stmt* s) {
  StrategyCatalogRepository::Row row;
  row.id = sqlite3_column_int64(s, 0);
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 1))) {
    row.kind = p;
  }
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 2))) {
    row.name = p;
  }
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 3))) {
    row.class_name = p;
  }
  row.enabled = sqlite3_column_int(s, 4) != 0;
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 5))) {
    row.hpp_path = p;
  }
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 6))) {
    row.cpp_path = p;
  }
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 7))) {
    row.sandbox_path = p;
  }
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 8))) {
    row.provenance = p;
  }
  row.compile_ok = sqlite3_column_int(s, 9) != 0;
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 10))) {
    row.created_at = p;
  }
  if (const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, 11))) {
    row.updated_at = p;
  }
  return row;
}

}  // namespace

StrategyCatalogRepository::StrategyCatalogRepository(sqlite3* db) : db_{db} {
  if (db_ == nullptr) {
    throw std::invalid_argument("StrategyCatalogRepository: null db");
  }
}

std::int64_t StrategyCatalogRepository::upsert_promoted(const Row& row) {
  Stmt st(db_,
          "INSERT INTO strategy_catalog "
          "(kind, name, class_name, enabled, hpp_path, cpp_path, sandbox_path, provenance, "
          " compile_ok, updated_at) "
          "VALUES (?, ?, ?, 0, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
          "ON CONFLICT(name) DO UPDATE SET "
          "  kind=excluded.kind, "
          "  class_name=excluded.class_name, "
          "  enabled=0, "
          "  hpp_path=excluded.hpp_path, "
          "  cpp_path=excluded.cpp_path, "
          "  sandbox_path=excluded.sandbox_path, "
          "  provenance=excluded.provenance, "
          "  compile_ok=excluded.compile_ok, "
          "  updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')");
  sqlite3_bind_text(st.s, 1, row.kind.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 2, row.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 3, row.class_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 4, row.hpp_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 5, row.cpp_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 6, row.sandbox_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 7, row.provenance.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st.s, 8, row.compile_ok ? 1 : 0);
  step_done(db_, st.s, "strategy_catalog upsert");

  const auto found = find_by_name(row.name);
  if (!found) {
    throw std::runtime_error("strategy_catalog upsert: row missing after write");
  }
  return found->id;
}

bool StrategyCatalogRepository::set_enabled(std::string_view name, bool enabled) {
  Stmt st(db_,
          "UPDATE strategy_catalog SET enabled=?, "
          "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE name=?");
  sqlite3_bind_int(st.s, 1, enabled ? 1 : 0);
  sqlite3_bind_text(st.s, 2, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
  step_done(db_, st.s, "strategy_catalog set_enabled");
  return sqlite3_changes(db_) > 0;
}

bool StrategyCatalogRepository::set_compile_ok(std::string_view name, bool ok,
                                               std::string_view sandbox_path) {
  Stmt st(db_,
          "INSERT INTO strategy_catalog "
          "(kind, name, enabled, sandbox_path, compile_ok, provenance, updated_at) "
          "VALUES ('strategy', ?, 0, ?, ?, 'agent', strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
          "ON CONFLICT(name) DO UPDATE SET "
          "  sandbox_path=excluded.sandbox_path, "
          "  compile_ok=excluded.compile_ok, "
          "  updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')");
  sqlite3_bind_text(st.s, 1, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(st.s, 2, sandbox_path.data(), static_cast<int>(sandbox_path.size()),
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(st.s, 3, ok ? 1 : 0);
  step_done(db_, st.s, "strategy_catalog set_compile_ok");
  return true;
}

std::optional<StrategyCatalogRepository::Row> StrategyCatalogRepository::find_by_name(
    std::string_view name) const {
  Stmt st(db_,
          "SELECT id, kind, name, class_name, enabled, hpp_path, cpp_path, sandbox_path, "
          "provenance, compile_ok, created_at, updated_at FROM strategy_catalog "
          "WHERE name=? LIMIT 1");
  sqlite3_bind_text(st.s, 1, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return std::nullopt;
  }
  return read_row(st.s);
}

std::vector<StrategyCatalogRepository::Row> StrategyCatalogRepository::list_all() const {
  Stmt st(db_,
          "SELECT id, kind, name, class_name, enabled, hpp_path, cpp_path, sandbox_path, "
          "provenance, compile_ok, created_at, updated_at FROM strategy_catalog "
          "ORDER BY name");
  std::vector<Row> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    out.push_back(read_row(st.s));
  }
  return out;
}

std::vector<StrategyCatalogRepository::Row> StrategyCatalogRepository::list_enabled() const {
  Stmt st(db_,
          "SELECT id, kind, name, class_name, enabled, hpp_path, cpp_path, sandbox_path, "
          "provenance, compile_ok, created_at, updated_at FROM strategy_catalog "
          "WHERE enabled=1 ORDER BY name");
  std::vector<Row> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    out.push_back(read_row(st.s));
  }
  return out;
}

}  // namespace algocraft
