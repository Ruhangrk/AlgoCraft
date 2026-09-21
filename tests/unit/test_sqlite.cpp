#include "algocraft/persistence/sqlite_database.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include <gtest/gtest.h>

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_sqlite" /
             (std::to_string(stamp) + "-" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

algocraft::PersistenceConfig make_config(const std::filesystem::path& dir) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir / "algocraft.db";
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
  return cfg;
}

}  // namespace

class SqliteDatabaseTest : public ::testing::Test {
protected:
  void SetUp() override { dir_ = make_temp_dir(); }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path dir_;
};

TEST_F(SqliteDatabaseTest, OpenMigrateCloseIdempotent) {
  const auto cfg = make_config(dir_);

  {
    algocraft::SqliteDatabase db(cfg);
    db.open();
    EXPECT_TRUE(db.is_open());
    EXPECT_EQ(db.journal_mode(), "wal");
    db.migrate();
    const auto tables = db.table_names();
    EXPECT_NE(std::find(tables.begin(), tables.end(), "schema_migrations"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "symbol_data_coverage"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "workbooks"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "runs"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "instruments"), tables.end());
    EXPECT_NE(std::find(tables.begin(), tables.end(), "backtests"), tables.end());
    EXPECT_EQ(db.applied_migrations(),
              (std::vector<std::string>{"schema_001.sql", "schema_002.sql", "schema_003.sql",
                                        "schema_004.sql", "schema_005.sql", "schema_006.sql"}));
    db.close();
    EXPECT_FALSE(db.is_open());
  }

  {
    algocraft::SqliteDatabase db(cfg);
    db.open();
    EXPECT_EQ(db.journal_mode(), "wal");
    db.migrate();
    EXPECT_EQ(db.applied_migrations(),
              (std::vector<std::string>{"schema_001.sql", "schema_002.sql", "schema_003.sql",
                                        "schema_004.sql", "schema_005.sql", "schema_006.sql"}));
    db.close();
  }

  EXPECT_TRUE(std::filesystem::exists(cfg.db_path));
}

TEST_F(SqliteDatabaseTest, CreatesParentDirectory) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir_ / "nested" / "store" / "algocraft.db";
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  EXPECT_TRUE(std::filesystem::exists(cfg.db_path));
  db.close();
}

TEST_F(SqliteDatabaseTest, EmptyMigrationsDirAppliesNoFiles) {
  const auto empty = dir_ / "empty_migrations";
  std::filesystem::create_directories(empty);

  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir_ / "algocraft.db";
  cfg.migrations_dir = empty;

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  EXPECT_TRUE(db.applied_migrations().empty());
  EXPECT_EQ(db.table_names(), (std::vector<std::string>{"schema_migrations"}));
  db.close();
}

TEST_F(SqliteDatabaseTest, MissingMigrationsDirThrows) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir_ / "algocraft.db";
  cfg.migrations_dir = dir_ / "does_not_exist";

  algocraft::SqliteDatabase db(cfg);
  db.open();
  EXPECT_THROW(db.migrate(), std::runtime_error);
  db.close();
}

TEST_F(SqliteDatabaseTest, ExecuteBeforeOpenThrows) {
  algocraft::SqliteDatabase db(make_config(dir_));
  EXPECT_THROW(db.execute("SELECT 1;"), std::runtime_error);
  EXPECT_THROW(db.migrate(), std::runtime_error);
}
