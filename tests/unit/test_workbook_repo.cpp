#include <gtest/gtest.h>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/workbook_repository.hpp"

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_wb_repo" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

TEST(WorkbookRepository, AddCapitalUpdatesMainAvailableAndEvent) {
  const auto dir = make_temp_dir();
  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir / "algocraft.db";
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  algocraft::WorkbookRepository repo(db.handle());

  const auto wid = repo.create(1, "cash", 10'00'000'00, "u1", "user");
  const auto before = repo.find(wid);
  ASSERT_TRUE(before);
  EXPECT_EQ(before->main_capital_paise, 10'00'000'00);
  EXPECT_EQ(before->available_paise, 10'00'000'00);

  const auto after = repo.add_capital(wid, 2'00'000'00);
  ASSERT_TRUE(after);
  EXPECT_EQ(after->main_capital_paise, 12'00'000'00);
  EXPECT_EQ(after->available_paise, 12'00'000'00);

  sqlite3_stmt* st = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(db.handle(),
                               "SELECT type, amount_paise FROM workbook_capital_events "
                               "WHERE workbook_id=? ORDER BY id ASC",
                               -1, &st, nullptr),
            SQLITE_OK);
  sqlite3_bind_int64(st, 1, wid);
  ASSERT_EQ(sqlite3_step(st), SQLITE_ROW);
  const auto* type = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
  ASSERT_NE(type, nullptr);
  EXPECT_STREQ(type, "capital_added");
  EXPECT_EQ(sqlite3_column_int64(st, 1), 2'00'000'00);
  sqlite3_finalize(st);

  EXPECT_FALSE(repo.add_capital(99999, 1).has_value());
  EXPECT_THROW((void)repo.add_capital(wid, 0), std::invalid_argument);

  db.close();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
