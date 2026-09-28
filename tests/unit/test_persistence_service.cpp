#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "algocraft/log/log.hpp"
#include "algocraft/log/log_hub.hpp"
#include "algocraft/persistence/persistence_service.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/workbook_repository.hpp"

namespace {

std::filesystem::path temp_dir(const char* name) {
  const auto dir =
      std::filesystem::temp_directory_path() / ("algocraft_persist_" + std::string(name));
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

algocraft::PersistenceConfig make_cfg(const std::filesystem::path& dir) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir / "test.db";
  cfg.migrations_dir = "migrations";
  return cfg;
}

}  // namespace

TEST(PersistenceService, DualOpenAndRunSyncWrite) {
  const auto dir = temp_dir("dual");
  algocraft::LogHub hub;
  algocraft::log::set_hub(&hub);

  algocraft::SqliteDatabase write_db(make_cfg(dir));
  write_db.open();
  write_db.migrate();

  algocraft::SqliteDatabase read_db(make_cfg(dir));
  read_db.open_readonly();

  algocraft::PersistenceService persist(write_db, hub);
  persist.start();

  std::int64_t wid = 0;
  persist.run_sync([&](sqlite3* db) {
    wid = algocraft::WorkbookRepository(db).create(1, "t2-wb", 1'00'000'00, "u", "user");
  });
  EXPECT_GT(wid, 0);
  EXPECT_GE(persist.jobs_done(), 1u);

  algocraft::WorkbookRepository readers(read_db.handle());
  const auto row = readers.find(wid);
  ASSERT_TRUE(row.has_value());
  EXPECT_EQ(row->name, "t2-wb");

  persist.stop();
  algocraft::log::set_hub(nullptr);
  std::filesystem::remove_all(dir);
}

TEST(PersistenceService, DrainsLogHub) {
  const auto dir = temp_dir("logs");
  algocraft::LogHub hub;
  algocraft::log::set_hub(&hub);

  algocraft::SqliteDatabase write_db(make_cfg(dir));
  write_db.open();
  write_db.migrate();

  algocraft::PersistenceService persist(write_db, hub);
  persist.start();

  AC_LOG_INFO("persist_service_drain_probe");
  persist.run_sync([](sqlite3*) {});  // nudge T2

  // Allow a couple drain cycles.
  for (int i = 0; i < 20 && hub.written() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_GE(hub.written(), 1u);

  persist.stop();
  algocraft::log::set_hub(nullptr);
  std::filesystem::remove_all(dir);
}

TEST(SqliteDatabase, OpenReadonlyRejectsWrite) {
  const auto dir = temp_dir("ro");
  algocraft::SqliteDatabase write_db(make_cfg(dir));
  write_db.open();
  write_db.migrate();
  write_db.close();

  algocraft::SqliteDatabase read_db(make_cfg(dir));
  read_db.open_readonly();
  EXPECT_THROW(
      {
        (void)algocraft::WorkbookRepository(read_db.handle())
            .create(1, "should-fail", 1, "u", "user");
      },
      std::runtime_error);
  std::filesystem::remove_all(dir);
}
