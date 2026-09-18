#include "algocraft/persistence/migration_runner.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "sqlite3.h"

namespace algocraft {
namespace {

constexpr const char* kEnsureTableSql =
    "CREATE TABLE IF NOT EXISTS schema_migrations ("
    "  filename TEXT PRIMARY KEY NOT NULL,"
    "  applied_at TEXT NOT NULL"
    ");";

struct Stmt {
  sqlite3_stmt* s{nullptr};

  Stmt() = default;
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  ~Stmt() { sqlite3_finalize(s); }
};

void exec_sql(sqlite3* db, const char* sql, const char* what) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = what;
    msg += ": ";
    msg += err != nullptr ? err : sqlite3_errmsg(db);
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot read migration: " + path.string());
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

}  // namespace

MigrationRunner::MigrationRunner(sqlite3* db, std::filesystem::path migrations_dir)
    : db_{db}, migrations_dir_{std::move(migrations_dir)} {
  if (db_ == nullptr) {
    throw std::invalid_argument("migration runner requires an open sqlite connection");
  }
}

void MigrationRunner::apply() {
  if (!std::filesystem::exists(migrations_dir_)) {
    throw std::runtime_error("migrations directory missing: " + migrations_dir_.string());
  }
  if (!std::filesystem::is_directory(migrations_dir_)) {
    throw std::runtime_error("migrations path is not a directory: " + migrations_dir_.string());
  }

  ensure_version_table();
  for (const auto& path : list_files()) {
    const auto name = path.filename().string();
    if (is_applied(name)) {
      continue;
    }
    apply_file(path);
  }
}

std::vector<std::string> MigrationRunner::applied() const {
  ensure_version_table();

  Stmt stmt;
  const int rc = sqlite3_prepare_v2(
      db_, "SELECT filename FROM schema_migrations ORDER BY filename;", -1, &stmt.s, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("prepare applied migrations: ") + sqlite3_errmsg(db_));
  }

  std::vector<std::string> out;
  int step = sqlite3_step(stmt.s);
  while (step == SQLITE_ROW) {
    const auto* text = sqlite3_column_text(stmt.s, 0);
    if (text != nullptr) {
      out.emplace_back(reinterpret_cast<const char*>(text));
    }
    step = sqlite3_step(stmt.s);
  }
  if (step != SQLITE_DONE) {
    throw std::runtime_error(std::string("read applied migrations: ") + sqlite3_errmsg(db_));
  }
  return out;
}

void MigrationRunner::ensure_version_table() const { exec_sql(db_, kEnsureTableSql, "schema_migrations"); }

void MigrationRunner::apply_file(const std::filesystem::path& path) const {
  const auto name = path.filename().string();
  const auto sql = read_file(path);

  exec_sql(db_, "BEGIN IMMEDIATE;", "begin migration");
  try {
    exec_sql(db_, sql.c_str(), name.c_str());

    Stmt stmt;
    const int rc = sqlite3_prepare_v2(
        db_, "INSERT INTO schema_migrations (filename, applied_at) VALUES (?, datetime('now'));",
        -1, &stmt.s, nullptr);
    if (rc != SQLITE_OK) {
      throw std::runtime_error(std::string("prepare migration record: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt.s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.s) != SQLITE_DONE) {
      throw std::runtime_error(std::string("record migration: ") + sqlite3_errmsg(db_));
    }
    exec_sql(db_, "COMMIT;", "commit migration");
  } catch (...) {
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    throw;
  }
}

bool MigrationRunner::is_applied(const std::string& filename) const {
  Stmt stmt;
  const int rc = sqlite3_prepare_v2(
      db_, "SELECT 1 FROM schema_migrations WHERE filename = ? LIMIT 1;", -1, &stmt.s, nullptr);
  if (rc != SQLITE_OK) {
    throw std::runtime_error(std::string("prepare is_applied: ") + sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt.s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
  const int step = sqlite3_step(stmt.s);
  if (step == SQLITE_ROW) {
    return true;
  }
  if (step == SQLITE_DONE) {
    return false;
  }
  throw std::runtime_error(std::string("is_applied: ") + sqlite3_errmsg(db_));
}

std::vector<std::filesystem::path> MigrationRunner::list_files() const {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(migrations_dir_)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (name.starts_with("schema_") && name.ends_with(".sql")) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace algocraft
