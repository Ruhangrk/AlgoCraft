#include <gtest/gtest.h>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/coverage_repository.hpp"
#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/rocks_bar_store.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

#ifndef ALGOCRAFT_DATA_DIR
#define ALGOCRAFT_DATA_DIR "data/1min"
#endif
#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_activity" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

std::int64_t uuid_low(const algocraft::Uuid& id) {
  std::uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val = (val << 8) | id.bytes[static_cast<std::size_t>(8 + i)];
  }
  return static_cast<std::int64_t>(val);
}

struct Fixture {
  std::filesystem::path dir;
  algocraft::SqliteDatabase db;
  algocraft::RocksBarStore bars;
  std::optional<algocraft::CoverageRepository> cov;

  Fixture()
      : dir(make_temp_dir()),
        db([](const std::filesystem::path& d) {
          algocraft::PersistenceConfig c;
          c.db_path = d / "algocraft.db";
          c.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
          c.bars_dir = d / "bars";
          return c;
        }(dir)),
        bars(dir / "bars") {
    db.open();
    db.migrate();
    bars.open();
    cov.emplace(db.handle());
  }

  ~Fixture() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

algocraft::RunConfig empty_run_config() {
  algocraft::RunConfig cfg{};
  cfg.user_id = algocraft::UserId::from_u64(2);
  cfg.workbook_name = "empty_wb";
  cfg.workbook_capital = algocraft::Capital::from_paise(1'00'000'00);
  cfg.tickers = {"RELIANCE"};
  cfg.strategies = {"consecutive_up_clip"};
  cfg.from = algocraft::Timestamp::from_nanos(1'789'065'000'000'000'000LL);
  cfg.to = algocraft::Timestamp::from_nanos(1'789'065'000'000'000'000LL);
  cfg.trade_from = cfg.from;
  cfg.trade_to = cfg.to;
  return cfg;
}

}  // namespace

TEST(ActivityRepository, PersistEmptyRunAndReadBack) {
  Fixture fix;
  algocraft::ActivityRepository repo(fix.db.handle());

  algocraft::SymbolTable symbols;
  symbols.intern({.ticker = "RELIANCE"}, {});

  auto cached = std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_DATA_DIR, &symbols), fix.bars, *fix.cov,
      symbols);
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);
  algocraft::WorkbookManager books;

  const auto cfg = empty_run_config();
  algocraft::RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols, &repo);

  const auto wb_db_id = uuid_low(result.workbook_id);
  const auto runs = repo.list_runs(wb_db_id);
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].fills, 0);
  EXPECT_EQ(runs[0].selected, 0);
  EXPECT_EQ(runs[0].returned_paise, cfg.workbook_capital.paise());
  EXPECT_TRUE(repo.list_containers(runs[0].id).empty());
  EXPECT_TRUE(repo.list_fills(runs[0].id).empty());
}

TEST(ActivityRepository, CapitalEventsMatchBorrowAndReturn) {
  Fixture fix;
  algocraft::ActivityRepository repo(fix.db.handle());

  algocraft::SymbolTable symbols;
  symbols.intern({.ticker = "RELIANCE"}, {});

  auto cached = std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_DATA_DIR, &symbols), fix.bars, *fix.cov,
      symbols);
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);
  algocraft::WorkbookManager books;

  const auto cfg = empty_run_config();
  algocraft::RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols, &repo);
  const auto wb_db_id = uuid_low(result.workbook_id);

  sqlite3_stmt* st = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(fix.db.handle(),
                               "SELECT type, amount_paise FROM workbook_capital_events "
                               "WHERE workbook_id=? ORDER BY id ASC",
                               -1, &st, nullptr),
            SQLITE_OK);
  sqlite3_bind_int64(st, 1, wb_db_id);

  std::vector<std::pair<std::string, std::int64_t>> events;
  while (sqlite3_step(st) == SQLITE_ROW) {
    std::string type;
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 0))) {
      type = t;
    }
    events.emplace_back(type, sqlite3_column_int64(st, 1));
  }
  sqlite3_finalize(st);

  ASSERT_GE(events.size(), 3u);
  EXPECT_EQ(events[0].first, "created");
  auto borrow_it = std::find_if(events.begin(), events.end(),
                                [](const auto& e) { return e.first == "borrow"; });
  ASSERT_NE(borrow_it, events.end());
  EXPECT_EQ(borrow_it->second, cfg.workbook_capital.paise());
  auto return_it = std::find_if(events.begin(), events.end(),
                                [](const auto& e) { return e.first == "return"; });
  ASSERT_NE(return_it, events.end());
  EXPECT_EQ(return_it->second, result.returned.paise());
}

TEST(ActivityRepository, PersistRunUnderExistingWorkbookId) {
  Fixture fix;
  algocraft::ActivityRepository repo(fix.db.handle());

  // Pre-create SQLite workbook id 99 (API path wid).
  {
    sqlite3_stmt* st = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(fix.db.handle(),
                                 "INSERT INTO users (id, username, password_hash, role) "
                                 "VALUES (9, 'bind-user', 'unset', 'user')",
                                 -1, &st, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(st), SQLITE_DONE);
    sqlite3_finalize(st);
  }
  {
    sqlite3_stmt* st = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(fix.db.handle(),
                                 "INSERT INTO workbooks (id, user_id, name, main_capital_paise, "
                                 "available_paise) VALUES (99, 9, 'bound_wb', 10000000, 10000000)",
                                 -1, &st, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(st), SQLITE_DONE);
    sqlite3_finalize(st);
  }

  algocraft::SymbolTable symbols;
  symbols.intern({.ticker = "RELIANCE"}, {});

  auto cached = std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_DATA_DIR, &symbols), fix.bars, *fix.cov,
      symbols);
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  const auto wb_id = algocraft::WorkbookId::from_u64(99);
  algocraft::WorkbookManager books;
  ASSERT_TRUE(books
                  .adopt(wb_id, algocraft::UserId::from_u64(9), "bound_wb",
                         algocraft::Capital::from_paise(1'00'000'00),
                         algocraft::Capital::from_paise(1'00'000'00))
                  .ok);

  auto cfg = empty_run_config();
  cfg.user_id = algocraft::UserId::from_u64(9);
  cfg.workbook_name = "bound_wb";
  cfg.existing_workbook_id = wb_id;

  algocraft::RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols, &repo);
  EXPECT_EQ(uuid_low(result.workbook_id), 99);

  const auto runs = repo.list_runs(99);
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].workbook_id, 99);
  EXPECT_TRUE(repo.list_runs(1).empty());
}
