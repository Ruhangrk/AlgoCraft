#include "algocraft/persistence/workbook_repository.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

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

void exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "unknown";
    sqlite3_free(err);
    throw std::runtime_error("sqlite exec: " + msg);
  }
}

}  // namespace

WorkbookRepository::WorkbookRepository(sqlite3* db) : db_{db} {
  if (db_ == nullptr) {
    throw std::invalid_argument("WorkbookRepository: null db");
  }
}

std::int64_t WorkbookRepository::create(std::int64_t user_id, std::string_view name,
                                        std::int64_t capital_paise, std::string_view username,
                                        std::string_view role) {
  auto step_done = [this](sqlite3_stmt* s, const char* what) {
    const int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
      const std::string err = sqlite3_errmsg(db_);
      throw std::runtime_error(std::string(what) + ": " + err + " (rc=" + std::to_string(rc) +
                               ")");
    }
  };

  exec(db_, "BEGIN");
  try {
    std::int64_t workbook_id = 1;
    {
      Stmt st(db_, "SELECT COALESCE(MAX(id), 0) + 1 FROM workbooks");
      if (sqlite3_step(st.s) == SQLITE_ROW) {
        workbook_id = sqlite3_column_int64(st.s, 0);
      }
    }
    {
      Stmt st(db_,
              "INSERT OR IGNORE INTO users (id, username, password_hash, role) "
              "VALUES (?, ?, 'unset', ?)");
      sqlite3_bind_int64(st.s, 1, user_id);
      sqlite3_bind_text(st.s, 2, username.data(), static_cast<int>(username.size()),
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(st.s, 3, role.data(), static_cast<int>(role.size()), SQLITE_TRANSIENT);
      step_done(st.s, "workbook create user");
    }
    {
      Stmt st(db_,
              "INSERT INTO workbooks (id, user_id, name, main_capital_paise, available_paise) "
              "VALUES (?, ?, ?, ?, ?)");
      sqlite3_bind_int64(st.s, 1, workbook_id);
      sqlite3_bind_int64(st.s, 2, user_id);
      sqlite3_bind_text(st.s, 3, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
      sqlite3_bind_int64(st.s, 4, capital_paise);
      sqlite3_bind_int64(st.s, 5, capital_paise);
      step_done(st.s, "workbook create");
    }
    exec(db_, "COMMIT");
    return workbook_id;
  } catch (...) {
    try {
      exec(db_, "ROLLBACK");
    } catch (...) {
    }
    throw;
  }
}

std::vector<WorkbookRepository::Row> WorkbookRepository::list_for_user(
    std::int64_t user_id) const {
  Stmt st(db_,
          "SELECT id, name, main_capital_paise, available_paise FROM workbooks "
          "WHERE user_id=? AND deleted_at IS NULL ORDER BY id");
  sqlite3_bind_int64(st.s, 1, user_id);
  std::vector<Row> out;
  while (sqlite3_step(st.s) == SQLITE_ROW) {
    Row row;
    row.id = sqlite3_column_int64(st.s, 0);
    if (const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1))) {
      row.name = name;
    }
    row.main_capital_paise = sqlite3_column_int64(st.s, 2);
    row.available_paise = sqlite3_column_int64(st.s, 3);
    out.push_back(std::move(row));
  }
  return out;
}

std::optional<WorkbookRepository::OwnerRow> WorkbookRepository::find_owner(
    std::int64_t workbook_id) const {
  Stmt st(db_,
          "SELECT user_id, name, available_paise FROM workbooks "
          "WHERE id=? AND deleted_at IS NULL");
  sqlite3_bind_int64(st.s, 1, workbook_id);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return std::nullopt;
  }
  OwnerRow row;
  row.user_id = sqlite3_column_int64(st.s, 0);
  if (const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1))) {
    row.name = name;
  }
  row.available_paise = sqlite3_column_int64(st.s, 2);
  return row;
}

std::optional<WorkbookRepository::Row> WorkbookRepository::find(
    std::int64_t workbook_id) const {
  Stmt st(db_,
          "SELECT id, name, main_capital_paise, available_paise FROM workbooks "
          "WHERE id=? AND deleted_at IS NULL");
  sqlite3_bind_int64(st.s, 1, workbook_id);
  if (sqlite3_step(st.s) != SQLITE_ROW) {
    return std::nullopt;
  }
  Row row;
  row.id = sqlite3_column_int64(st.s, 0);
  if (const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(st.s, 1))) {
    row.name = name;
  }
  row.main_capital_paise = sqlite3_column_int64(st.s, 2);
  row.available_paise = sqlite3_column_int64(st.s, 3);
  return row;
}

bool WorkbookRepository::can_access(std::int64_t workbook_id, std::int64_t user_id,
                                    bool is_admin) const {
  const auto owner = find_owner(workbook_id);
  if (!owner) {
    return false;
  }
  return is_admin || owner->user_id == user_id;
}

}  // namespace algocraft
