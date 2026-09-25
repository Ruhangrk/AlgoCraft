#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "algocraft/backtest/backtest_service.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/session_calendar.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/persistence/backtest_repository.hpp"
#include "algocraft/persistence/coverage_repository.hpp"
#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/rocks_bar_store.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_bt_svc" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

algocraft::Timestamp ist_ns(int y, int mon, int d, int hour, int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{y} / mon / d} + hours{hour} + minutes{minute} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

// Flat → spike → drop so ema_crossover (fast=2, slow=3) produces fills.
void write_ema_session(const std::filesystem::path& path) {
  std::ofstream out(path);
  out << "timestamp_ns,open,high,low,close,volume\n";
  const double closes[] = {100, 100, 100, 100, 100, 100, 100, 100, 120, 120,
                           120, 120, 120, 120, 80,  80,  80,  80};
  for (int i = 0; i < 18; ++i) {
    const auto ts = ist_ns(2026, 8, 28, 9, 15 + i);
    const auto c = closes[i];
    out << ts.nanos() << "," << c << "," << c << "," << c << "," << c << ",1000\n";
  }
}

}  // namespace

TEST(BacktestService, PersistDeterministicEmaWithoutTouchingWorkbookCapital) {
  const auto dir = make_temp_dir();
  write_ema_session(dir / "EMA.csv");

  algocraft::PersistenceConfig cfg;
  cfg.db_path = dir / "algocraft.db";
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
  cfg.bars_dir = dir / "bars";

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  algocraft::RocksBarStore bars(dir / "bars");
  bars.open();
  algocraft::CoverageRepository cov(db.handle());
  algocraft::WorkbookRepository workbooks(db.handle());
  algocraft::BacktestRepository backtests(db.handle());

  const auto wid = workbooks.create(1, "bt-wb", 10'00'000'00, "bt-user", "user");

  algocraft::SymbolTable symbols;
  symbols.intern({.ticker = "EMA"}, {});
  auto csv = std::make_unique<algocraft::CsvProvider>(dir, &symbols);
  auto cached = std::make_unique<algocraft::CachedProvider>(std::move(csv), bars, cov, symbols);
  cached->fetch().set_now(ist_ns(2026, 8, 28, 18, 0));
  algocraft::DataSourceRegistry registry;
  auto* fetch = &cached->fetch();
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  algocraft::BacktestService svc(workbooks, backtests, registry, *fetch, strategies, symbols);

  algocraft::ManualBacktestRequest req{};
  req.workbook_id = wid;
  req.ticker = "EMA";
  req.strategy_name = "ema_crossover";
  req.capital = algocraft::Capital::from_paise(1'00'000'00);
  req.from = ist_ns(2026, 8, 28, 9, 15);
  req.to = ist_ns(2026, 8, 28, 15, 30);
  req.strategy.ema_fast = 2;
  req.strategy.ema_slow = 3;
  req.strategy.order_qty = algocraft::Quantity::from_shares(1);

  const auto before = workbooks.find(wid)->available_paise;
  const auto a = svc.run(req);
  EXPECT_GT(a.row.id, 0);
  EXPECT_GT(a.result.fills, 0);
  EXPECT_EQ(a.row.fills, a.result.fills);
  EXPECT_EQ(a.row.pnl_paise, a.result.realized_pnl_paise);
  EXPECT_EQ(a.row.fees_paise, a.result.fees_paise);
  EXPECT_EQ(a.row.return_pct_bp, (a.row.pnl_paise * 10000) / a.row.capital_paise);
  EXPECT_EQ(a.row.status, "completed");

  // Manual backtest must not touch workbook available capital.
  EXPECT_EQ(workbooks.find(wid)->available_paise, before);

  const auto listed = backtests.list_for_workbook(wid);
  ASSERT_EQ(listed.size(), 1u);
  EXPECT_EQ(listed[0].id, a.row.id);

  const auto b = svc.run(req);
  EXPECT_EQ(a.result.realized_pnl_paise, b.result.realized_pnl_paise);
  EXPECT_EQ(a.result.fees_paise, b.result.fees_paise);
  EXPECT_EQ(a.result.fills, b.result.fills);
  EXPECT_EQ(backtests.list_for_workbook(wid).size(), 2u);

  EXPECT_TRUE(backtests.soft_delete(wid, a.row.id));
  EXPECT_FALSE(backtests.find(wid, a.row.id).has_value());
  const auto after_del = backtests.list_for_workbook(wid);
  ASSERT_EQ(after_del.size(), 1u);
  EXPECT_EQ(after_del[0].id, b.row.id);
  EXPECT_FALSE(backtests.soft_delete(wid, a.row.id));

  algocraft::BacktestListFilter page{};
  page.limit = 1;
  page.cursor = b.row.id + 1;
  const auto paged = backtests.list_for_workbook(wid, page);
  ASSERT_EQ(paged.size(), 1u);
  EXPECT_EQ(paged[0].id, b.row.id);

  // Soft-deleted backtest a still has orphan event rows; b is live and must have timeline data.
  EXPECT_EQ(static_cast<int>(backtests.list_fills(b.row.id).size()), b.result.fills);
  EXPECT_FALSE(backtests.list_signals(b.row.id).empty());

  db.close();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
