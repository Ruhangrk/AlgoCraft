#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "algocraft/market_data/instrument_ingest.hpp"
#include "algocraft/persistence/instrument_repository.hpp"
#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/sqlite_database.hpp"

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif
#ifndef ALGOCRAFT_TEST_DATA_DIR
#define ALGOCRAFT_TEST_DATA_DIR "tests/data"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_instruments" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

TEST(InstrumentIngest, ParsesFixtureKeepsIneOnly) {
  const auto path =
      std::filesystem::path(ALGOCRAFT_TEST_DATA_DIR) / "instruments_nse_eq_sample.csv";
  const auto csv = algocraft::load_upstox_instruments_csv(path.string());
  std::vector<algocraft::InstrumentRow> rows;
  const auto stats = algocraft::parse_upstox_instruments_csv(csv, rows);
  EXPECT_EQ(stats.rows_read, 6);
  EXPECT_EQ(stats.rows_kept, 4);
  EXPECT_EQ(stats.rows_skipped, 2);
  ASSERT_EQ(rows.size(), 4u);

  bool has_reliance = false;
  for (const auto& r : rows) {
    EXPECT_EQ(r.exchange, "NSE");
    EXPECT_EQ(r.segment, "EQ");
    EXPECT_TRUE(r.isin.rfind("INE", 0) == 0);
    EXPECT_FALSE(r.instrument_key.empty());
    if (r.ticker == "RELIANCE") {
      has_reliance = true;
      EXPECT_EQ(r.isin, "INE002A01018");
      EXPECT_EQ(r.instrument_key, "NSE_EQ|INE002A01018");
      EXPECT_EQ(r.tick_size_paise, 10);
    }
  }
  EXPECT_TRUE(has_reliance);
}

TEST(InstrumentRepository, UpsertIdempotentAndSearch) {
  const auto dir = make_temp_dir();
  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir / "algocraft.db";
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  algocraft::InstrumentRepository repo(db.handle());

  const auto path =
      std::filesystem::path(ALGOCRAFT_TEST_DATA_DIR) / "instruments_nse_eq_sample.csv";
  const auto first = algocraft::ingest_upstox_instruments(repo, path.string());
  EXPECT_EQ(first.rows_kept, 4);
  EXPECT_EQ(repo.count_active(), 4);

  const auto second = algocraft::ingest_upstox_instruments(repo, path.string());
  EXPECT_EQ(second.rows_kept, 4);
  EXPECT_EQ(repo.count_active(), 4);

  const auto reliance = repo.find_by_ticker("RELIANCE");
  ASSERT_TRUE(reliance.has_value());
  EXPECT_EQ(reliance->name, "RELIANCE INDUSTRIES LTD");

  const auto hits = repo.search("RELI", 10);
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits.front().ticker, "RELIANCE");

  const auto none = repo.find_by_ticker("NOT_A_TICKER");
  EXPECT_FALSE(none.has_value());

  db.close();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
