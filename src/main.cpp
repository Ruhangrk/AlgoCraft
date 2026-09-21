#include "algocraft/api/http_server.hpp"
#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/engine/phase0_runtime.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/dummy_provider.hpp"
#include "algocraft/market_data/upstox_provider.hpp"
#include "algocraft/market_data/instrument_ingest.hpp"
#include "algocraft/persistence/instrument_repository.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/coverage_repository.hpp"
#include "algocraft/persistence/rocks_bar_store.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#ifndef ALGOCRAFT_DATA_DIR
#define ALGOCRAFT_DATA_DIR "data/1min"
#endif
#ifndef ALGOCRAFT_DB_PATH
#define ALGOCRAFT_DB_PATH "data/algocraft.db"
#endif
#ifndef ALGOCRAFT_BARS_DIR
#define ALGOCRAFT_BARS_DIR "data/bars"
#endif
#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

std::tm ist_tm(std::int64_t timestamp_ns) {
  const auto ist_sec = static_cast<std::time_t>(timestamp_ns / 1'000'000'000LL + 19800);
  std::tm out{};
  gmtime_r(&ist_sec, &out);
  return out;
}

algocraft::PersistenceConfig engine_persistence_cfg() {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = ALGOCRAFT_DB_PATH;
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
  cfg.bars_dir = ALGOCRAFT_BARS_DIR;
  return cfg;
}

struct EngineCache {
  algocraft::SqliteDatabase db;
  algocraft::RocksBarStore bars;
  std::optional<algocraft::CoverageRepository> coverage;

  EngineCache() : db(engine_persistence_cfg()), bars(ALGOCRAFT_BARS_DIR) {
    db.open();
    db.migrate();
    bars.open();
    coverage.emplace(db.handle());
    spdlog::info("sqlite={} rocksdb={}", db.path().string(), bars.path().string());
  }

  algocraft::CoverageRepository& cov() { return *coverage; }
};

std::unique_ptr<algocraft::CachedProvider> wrap_csv(const char* data_dir,
                                                    algocraft::SymbolTable& symbols,
                                                    EngineCache& cache) {
  return std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(data_dir, &symbols), cache.bars, cache.cov(),
      symbols);
}

int run_db_smoke(const char* db_path) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = db_path;
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  const auto tables = db.table_names();
  const auto applied = db.applied_migrations();
  spdlog::info("sqlite path={} wal={} tables={} migrations={}", db.path().string(),
               db.journal_mode(), tables.size(), applied.size());
  for (const auto& name : applied) {
    spdlog::info("  applied {}", name);
  }
  db.close();
  return db.is_open() ? 1 : 0;
}

// algocraft_engine instruments ingest [source] [db_path]
// source defaults to Upstox complete.csv.gz URL; may be a local .csv / .csv.gz path.
int run_instruments_ingest(const char* source, const char* db_path) {
  algocraft::PersistenceConfig cfg;
  cfg.db_path = db_path;
  cfg.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;

  algocraft::SqliteDatabase db(cfg);
  db.open();
  db.migrate();
  algocraft::InstrumentRepository repo(db.handle());
  spdlog::info("instruments ingest source={}", source);
  const auto stats = algocraft::ingest_upstox_instruments(repo, source);
  spdlog::info("instruments read={} kept={} skipped={} active={}", stats.rows_read,
               stats.rows_kept, stats.rows_skipped, repo.count_active());
  db.close();
  return 0;
}

int run_phase0_smoke() {
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<algocraft::DummyProvider>());
  spdlog::info("active data source: {}", registry.active_provider().name());

  algocraft::Phase0Runtime runtime;
  runtime.start();

  constexpr std::uint64_t kBars = 8;
  for (std::uint64_t i = 0; i < kBars; ++i) {
    algocraft::DummyEvent event{};
    event.kind = algocraft::kEventBar;
    event.symbol_id = 1;
    event.seq = i;
    while (!runtime.market_data_ring().try_push(event)) {
      std::this_thread::yield();
    }
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (runtime.bars_processed() < kBars && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  runtime.stop();
  spdlog::info("bars={} fills={} commands={} persist={} logs={}", runtime.bars_processed(),
               runtime.fills_processed(), runtime.commands_processed(), runtime.persist_events(),
               runtime.logs_written());
  return runtime.bars_processed() >= kBars ? 0 : 1;
}

int run_clip_backtest(const char* data_dir) {
  algocraft::SymbolTable symbols;
  const algocraft::Instrument inst{};
  const std::array<const char*, 3> tickers{"RELIANCE", "INFY", "TCS"};
  std::array<algocraft::SymbolId, 3> ids{};
  for (std::size_t i = 0; i < tickers.size(); ++i) {
    ids[i] = symbols.intern({.ticker = tickers[i]}, inst);
  }

  EngineCache cache;
  auto cached = wrap_csv(data_dir, symbols, cache);
  auto* fetch = &cached->fetch();
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);
  algocraft::BacktestRunner runner;

  spdlog::info("consecutive_up_clip capital=2cr/stock clip=20L data={}", data_dir);
  for (std::size_t i = 0; i < tickers.size(); ++i) {
    algocraft::BacktestRequest req{};
    req.symbol_id = ids[i];
    req.strategy_name = "consecutive_up_clip";
    req.starting_capital = algocraft::Capital::from_paise(2'00'00'000'00);
    req.strategy.clip_paise = 20'00'000'00;
    const auto r = runner.run(registry, strategies, req);
    const auto ret_pct =
        r.starting_capital_paise == 0
            ? 0.0
            : 100.0 * static_cast<double>(r.ending_equity_paise - r.starting_capital_paise) /
                  static_cast<double>(r.starting_capital_paise);
    spdlog::info(
        "{} bars={} fills={} trips={} pnl_rupees={:.2f} fees_rupees={:.2f} return_pct={:.4f} "
        "sharpe={:.4f} max_dd_rupees={:.2f} win_rate={:.3f}",
        tickers[i], r.bars, r.fills, r.round_trips, r.realized_pnl_paise / 100.0,
        r.fees_paise / 100.0, ret_pct, r.sharpe, r.max_drawdown_paise / 100.0, r.win_rate);

    std::size_t fill_i = 0;
    for (const auto& day : r.daily) {
      const auto d = ist_tm(day.timestamp_ns);
      spdlog::info("  {:04d}-{:02d}-{:02d} pnl_rs={:.2f} fees_rs={:.2f} fills={} trips={} wins={}",
                   d.tm_year + 1900, d.tm_mon + 1, d.tm_mday, day.realized_pnl_paise / 100.0,
                   day.fees_paise / 100.0, day.fills, day.round_trips, day.wins);
      const auto day_key = day.timestamp_ns / 86'400'000'000'000LL;
      while (fill_i < r.fills_log.size() &&
             r.fills_log[fill_i].timestamp_ns / 86'400'000'000'000LL == day_key) {
        const auto& f = r.fills_log[fill_i];
        const auto t = ist_tm(f.timestamp_ns);
        spdlog::info("    {:02d}:{:02d} {} qty={} px={:.2f} fees={:.2f}", t.tm_hour, t.tm_min,
                     f.side == algocraft::Side::Buy ? "BUY " : "SELL", f.qty, f.price_paise / 100.0,
                     f.fees_paise / 100.0);
        ++fill_i;
      }
    }
  }
  spdlog::info("vendor_fetches={}", fetch->vendor_fetches());
  return 0;
}

int run_phase2_backtest(const char* data_dir) {
  algocraft::SymbolTable symbols;
  const algocraft::Instrument inst{};
  const std::array<const char*, 3> tickers{"RELIANCE", "INFY", "TCS"};
  std::array<algocraft::SymbolId, 3> ids{};
  for (std::size_t i = 0; i < tickers.size(); ++i) {
    ids[i] = symbols.intern({.ticker = tickers[i]}, inst);
  }

  EngineCache cache;
  auto cached = wrap_csv(data_dir, symbols, cache);
  auto* fetch = &cached->fetch();
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));
  spdlog::info("active data source: {} dir={}", registry.active_provider().name(), data_dir);

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  const std::array<const char*, 3> names{"ema_crossover", "vwap_reversion",
                                         "opening_range_breakout"};

  algocraft::BacktestRunner runner;
  for (const char* strategy_name : names) {
    for (std::size_t i = 0; i < tickers.size(); ++i) {
      algocraft::BacktestRequest req{};
      req.symbol_id = ids[i];
      req.strategy_name = strategy_name;
      req.starting_capital = algocraft::Capital::from_paise(10'00'000'00);
      req.strategy.order_qty = algocraft::Quantity::from_shares(50);
      const auto r = runner.run(registry, strategies, req);
      const auto ret_pct =
          r.starting_capital_paise == 0
              ? 0.0
              : 100.0 * static_cast<double>(r.ending_equity_paise - r.starting_capital_paise) /
                    static_cast<double>(r.starting_capital_paise);
      spdlog::info(
          "{} {} bars={} fills={} trips={} pnl_paise={} fees_paise={} equity_paise={} "
          "return_pct={:.4f} sharpe={:.4f} max_dd_paise={} win_rate={:.3f} avg_hold_s={:.1f}",
          r.strategy_name, tickers[i], r.bars, r.fills, r.round_trips, r.realized_pnl_paise,
          r.fees_paise, r.ending_equity_paise, ret_pct, r.sharpe, r.max_drawdown_paise, r.win_rate,
          r.avg_hold_seconds);
    }
  }
  spdlog::info("vendor_fetches={}", fetch->vendor_fetches());
  return 0;
}

int run_phase4(const char* data_dir) {
  algocraft::SymbolTable symbols;
  const algocraft::Instrument inst{};
  const std::array<const char*, 10> tickers{"RELIANCE", "INFY",     "TCS", "HDFCBANK", "ICICIBANK",
                                            "SBIN",     "BHARTIARTL", "ITC", "LT",      "HINDUNILVR"};
  for (const char* ticker : tickers) {
    symbols.intern({.ticker = ticker}, inst);
  }

  EngineCache cache;
  auto cached = wrap_csv(data_dir, symbols, cache);
  auto* fetch = &cached->fetch();
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  algocraft::WorkbookManager books;
  algocraft::RunConfig cfg{};
  cfg.user_id = algocraft::UserId::from_u64(1);
  cfg.workbook_name = "phase4-10cr";
  cfg.workbook_capital = algocraft::Capital::from_paise(10'00'00'000'00);
  for (const char* ticker : tickers) {
    cfg.tickers.emplace_back(ticker);
  }
  cfg.strategies = {"ema_crossover", "vwap_reversion", "consecutive_up_clip"};
  cfg.from = algocraft::Timestamp::from_nanos(1'787'509'800'000'000'000LL);
  cfg.to = algocraft::Timestamp::from_nanos(1'789'064'940'000'000'000LL);
  cfg.trade_from = algocraft::Timestamp::from_nanos(1'789'065'000'000'000'000LL);
  cfg.trade_to = algocraft::Timestamp::from_nanos(1'789'151'340'000'000'000LL);

  spdlog::info(
      "phase4 default_router 10 stocks x ema/vwap/clip, eval=14 sessions "
      "2026-08-24..2026-09-10, trade=2026-09-11, capital=10cr data={}",
      data_dir);
  algocraft::ActivityRepository act_repo(cache.db.handle());
  algocraft::RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols, &act_repo);

  std::int64_t eval_pnl = 0;
  spdlog::info("--- 14-day eval (₹10L/pair) selected={} skipped={}", result.selected,
               result.skipped);
  for (const auto& ev : result.evaluations) {
    eval_pnl += ev.pnl_paise;
    spdlog::info("  {} {} bars={} fills={} pnl_rs={:.2f} {}", ev.ticker, ev.strategy_name, ev.bars,
                 ev.fills, ev.pnl_paise / 100.0, ev.selected ? "WINNER" : "SKIP");
  }
  spdlog::info("  eval_total_pnl_rs={:.2f}", eval_pnl / 100.0);

  const auto trade_pnl = result.returned.paise() - cfg.workbook_capital.paise();
  spdlog::info(
      "--- 15th day REAL 2026-09-11 containers={} fills={} trade_pnl_rs={:.2f} "
      "returned_rs={:.2f} workbook_after_rs={:.2f} force_stop={}",
      result.real_containers, result.fills, trade_pnl / 100.0, result.returned.paise() / 100.0,
      result.workbook_available_after.paise() / 100.0, result.force_stopped);
  for (const auto& row : result.traded) {
    spdlog::info("  {} {} alloc_rs={:.2f} realized_rs={:.2f} cash_rs={:.2f} fills={}", row.ticker,
                 row.strategy_name, row.allocation.paise() / 100.0, row.realized.paise() / 100.0,
                 row.cash.paise() / 100.0, row.fills);
  }
  spdlog::info("vendor_fetches={}", fetch->vendor_fetches());
  return 0;
}

int run_api_server(const char* data_dir, int port) {
  EngineCache cache;
  algocraft::SymbolTable symbols;
  const algocraft::Instrument inst{};
  // DefaultRouter universe (10). Ensure/API may use any of these.
  for (const char* t : {"RELIANCE", "INFY", "TCS", "HDFCBANK", "ICICIBANK", "SBIN", "BHARTIARTL",
                        "ITC", "LT", "HINDUNILVR"}) {
    symbols.intern({.ticker = t}, inst);
  }

  algocraft::DataFetchService* fetch_ptr = nullptr;
  std::unique_ptr<algocraft::CachedProvider> cached;

  auto upstox_cfg = algocraft::UpstoxConfig::from_default_file();
  if (upstox_cfg.ok()) {
    auto upstox = std::make_unique<algocraft::UpstoxProvider>(std::move(upstox_cfg), &symbols);
    cached = std::make_unique<algocraft::CachedProvider>(std::move(upstox), cache.bars, cache.cov(),
                                                         symbols);
    spdlog::info("data source: upstox (token loaded from ~/.config/upstox/config.json)");
  } else {
    cached = wrap_csv(data_dir, symbols, cache);
    spdlog::warn("upstox token missing — serving CSV from {}", data_dir);
  }
  fetch_ptr = &cached->fetch();

  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(cached));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  algocraft::HttpServer::Config cfg;
  cfg.host = "127.0.0.1";
  cfg.port = port;
  algocraft::HttpServer server(cfg, cache.db, registry, strategies, symbols, fetch_ptr);
  spdlog::info("API listening on http://{}:{}", cfg.host, cfg.port);
  server.start();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "db") == 0) {
    const char* db_path = (argc >= 3) ? argv[2] : ALGOCRAFT_DB_PATH;
    return run_db_smoke(db_path);
  }
  if (argc >= 2 && std::strcmp(argv[1], "instruments") == 0) {
    if (argc >= 3 && std::strcmp(argv[2], "ingest") == 0) {
      const char* source =
          (argc >= 4) ? argv[3] : algocraft::kUpstoxInstrumentsUrl.data();
      const char* db_path = (argc >= 5) ? argv[4] : ALGOCRAFT_DB_PATH;
      return run_instruments_ingest(source, db_path);
    }
    spdlog::error("usage: algocraft_engine instruments ingest [source] [db_path]");
    return 1;
  }
  if (argc >= 2 && std::strcmp(argv[1], "serve") == 0) {
    const char* data_dir = (argc >= 3) ? argv[2] : ALGOCRAFT_DATA_DIR;
    int port = 8080;
    if (argc >= 4) {
      port = std::atoi(argv[3]);
    }
    return run_api_server(data_dir, port);
  }
  if (argc >= 2 && std::strcmp(argv[1], "run") == 0) {
    const char* data_dir = (argc >= 3) ? argv[2] : ALGOCRAFT_DATA_DIR;
    return run_phase4(data_dir);
  }
  if (argc >= 2 && std::strcmp(argv[1], "backtest") == 0) {
    if (argc >= 3 && std::strcmp(argv[2], "consecutive_up_clip") == 0) {
      const char* data_dir = (argc >= 4) ? argv[3] : ALGOCRAFT_DATA_DIR;
      return run_clip_backtest(data_dir);
    }
    const char* data_dir = (argc >= 3) ? argv[2] : ALGOCRAFT_DATA_DIR;
    return run_phase2_backtest(data_dir);
  }
  return run_phase0_smoke();
}
