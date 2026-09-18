#include "algocraft/persistence/sqlite_database.hpp"

#include <stdexcept>
#include <utility>

#include "algocraft/persistence/migration_runner.hpp"
#include "sqlite3.h"

namespace algocraft {
namespace {

[[noreturn]] void raise_sqlite(sqlite3* db, const char* what, int rc) {
  std::string msg = what;
  msg += ": ";
  if (db != nullptr) {
    const char* err = sqlite3_errmsg(db);
    msg += err != nullptr ? err : sqlite3_errstr(rc);
  } else {
    msg += sqlite3_errstr(rc);
  }
  throw std::runtime_error(msg);
}

int first_column_cb(void* p, int argc, char** cols, char** /*names*/) {
  auto* out = static_cast<std::string*>(p);
  if (argc > 0 && cols != nullptr && cols[0] != nullptr) {
    *out = cols[0];
  }
  return 0;
}

int collect_column_cb(void* p, int argc, char** cols, char** /*names*/) {
  auto* out = static_cast<std::vector<std::string>*>(p);
  if (argc > 0 && cols != nullptr && cols[0] != nullptr) {
    out->emplace_back(cols[0]);
  }
  return 0;
}

}  // namespace

void SqliteDatabase::Closer::operator()(sqlite3* db) const noexcept {
  if (db != nullptr) {
    sqlite3_close_v2(db);
  }
}

SqliteDatabase::SqliteDatabase(PersistenceConfig config) : config_{std::move(config)} {}

SqliteDatabase::~SqliteDatabase() { close(); }

void SqliteDatabase::open() {
  if (db_ != nullptr) {
    return;
  }

  if (const auto parent = config_.db_path.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  sqlite3* raw = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  const int rc = sqlite3_open_v2(config_.db_path.c_str(), &raw, flags, nullptr);
  std::unique_ptr<sqlite3, Closer> opened{raw};
  if (rc != SQLITE_OK) {
    raise_sqlite(raw, "sqlite open failed", rc);
  }
  db_ = std::move(opened);

  try {
    apply_pragmas();
  } catch (...) {
    close();
    throw;
  }
}

void SqliteDatabase::close() {
  db_.reset();
  journal_mode_.clear();
}

void SqliteDatabase::migrate() {
  ensure_open();
  MigrationRunner runner(db_.get(), config_.migrations_dir);
  runner.apply();
}

void SqliteDatabase::execute(std::string_view sql) {
  ensure_open();
  const std::string owned{sql};
  char* err = nullptr;
  const int rc = sqlite3_exec(db_.get(), owned.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err != nullptr ? err : sqlite3_errmsg(db_.get());
    sqlite3_free(err);
    throw std::runtime_error("sqlite: " + msg);
  }
}

std::vector<std::string> SqliteDatabase::table_names() const {
  ensure_open();
  std::vector<std::string> names;
  char* err = nullptr;
  const int rc = sqlite3_exec(
      db_.get(),
      "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' "
      "ORDER BY name",
      collect_column_cb, &names, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err != nullptr ? err : sqlite3_errmsg(db_.get());
    sqlite3_free(err);
    throw std::runtime_error("sqlite: " + msg);
  }
  return names;
}

std::vector<std::string> SqliteDatabase::applied_migrations() const {
  ensure_open();
  return MigrationRunner(db_.get(), config_.migrations_dir).applied();
}

void SqliteDatabase::apply_pragmas() {
  std::string mode;
  char* err = nullptr;
  int rc = sqlite3_exec(db_.get(), "PRAGMA journal_mode=WAL;", first_column_cb, &mode, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err != nullptr ? err : sqlite3_errmsg(db_.get());
    sqlite3_free(err);
    throw std::runtime_error("sqlite WAL: " + msg);
  }
  // In-memory databases stay in "memory" journal mode; that's fine for tests.
  if (mode != "wal" && mode != "WAL" && mode != "memory") {
    throw std::runtime_error("failed to enable WAL, journal_mode=" + mode);
  }
  journal_mode_ = mode;

  execute("PRAGMA synchronous=NORMAL;");
  execute("PRAGMA foreign_keys=ON;");
  execute("PRAGMA busy_timeout=5000;");
}

void SqliteDatabase::ensure_open() const {
  if (db_ == nullptr) {
    throw std::runtime_error("sqlite database is closed");
  }
}

}  // namespace algocraft
