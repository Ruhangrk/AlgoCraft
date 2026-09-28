#include "algocraft/api/http_server.hpp"
#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/engine/phase0_runtime.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/log/log.hpp"
#include "algocraft/log/log_hub.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/dummy_provider.hpp"
#include "algocraft/market_data/upstox_provider.hpp"
#include "algocraft/market_data/instrument_ingest.hpp"
#include "algocraft/persistence/instrument_repository.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/coverage_repository.hpp"
#include "algocraft/persistence/persistence_service.hpp"
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
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

#include <sys/file.h>

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

// Held for process lifetime so a second `serve` fails before RocksDB open.
int g_serve_lock_fd = -1;

struct PeeledArgs {
  std::optional<algocraft::LogLevel> level;
  std::vector<const char*> rest;
};

// Pulls known level tokens out of argv[start..). Remaining keep order.
PeeledArgs peel_log_level(int argc, char** argv, int start) {
  PeeledArgs out;
  for (int i = start; i < argc; ++i) {
    if (auto lvl = algocraft::parse_log_level(argv[i])) {
      out.level = *lvl;
      continue;
    }
    out.rest.push_back(argv[i]);
  }
  return out;
}

const char* arg_or(const PeeledArgs& peeled, std::size_t index, const char* fallback) {
  return index < peeled.rest.size() ? peeled.rest[index] : fallback;
}

std::string list_algocraft_engine_procs() {
  std::string out;
  DIR* dir = opendir("/proc");
  if (dir == nullptr) {
    return out;
  }
  const auto self = ::getpid();
  while (dirent* ent = readdir(dir)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') {
      continue;
    }
    const auto pid = static_cast<::pid_t>(std::strtol(ent->d_name, nullptr, 10));
    if (pid == self) {
      continue;
    }
    const std::string base = std::string("/proc/") + ent->d_name;
    char exe[512];
    const auto n = readlink((base + "/exe").c_str(), exe, sizeof(exe) - 1);
    if (n <= 0) {
      continue;
    }
    exe[n] = '\0';
    if (std::string_view{exe}.find("algocraft_engine") == std::string_view::npos) {
      continue;
    }
    std::ifstream cmdf(base + "/cmdline");
    std::string cmd((std::istreambuf_iterator<char>(cmdf)), std::istreambuf_iterator<char>());
    for (char& c : cmd) {
      if (c == '\0') {
        c = ' ';
      }
    }
    out += "  pid=";
    out += ent->d_name;
    out += " ";
    out += cmd;
    out += '\n';
  }
  closedir(dir);
  return out;
}

bool other_serve_running() {
  DIR* dir = opendir("/proc");
  if (dir == nullptr) {
    return false;
  }
  const auto self = ::getpid();
  bool found = false;
  while (dirent* ent = readdir(dir)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') {
      continue;
    }
    const auto pid = static_cast<::pid_t>(std::strtol(ent->d_name, nullptr, 10));
    if (pid == self) {
      continue;
    }
    const std::string base = std::string("/proc/") + ent->d_name;
    char exe[512];
    const auto n = readlink((base + "/exe").c_str(), exe, sizeof(exe) - 1);
    if (n <= 0) {
      continue;
    }
    exe[n] = '\0';
    if (std::string_view{exe}.find("algocraft_engine") == std::string_view::npos) {
      continue;
    }
    std::ifstream cmdf(base + "/cmdline");
    std::string cmd((std::istreambuf_iterator<char>(cmdf)), std::istreambuf_iterator<char>());
    for (char& c : cmd) {
      if (c == '\0') {
        c = ' ';
      }
    }
    if (cmd.find("serve") != std::string::npos) {
      found = true;
      break;
    }
  }
  closedir(dir);
  return found;
}

bool acquire_serve_lock(const std::filesystem::path& lock_path) {
  std::filesystem::create_directories(lock_path.parent_path());
  g_serve_lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0644);
  if (g_serve_lock_fd < 0) {
    return false;
  }
  if (::flock(g_serve_lock_fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(g_serve_lock_fd);
    g_serve_lock_fd = -1;
    return false;
  }
  const auto pid = std::to_string(::getpid()) + "\n";
  (void)::ftruncate(g_serve_lock_fd, 0);
  (void)::lseek(g_serve_lock_fd, 0, SEEK_SET);
  (void)::write(g_serve_lock_fd, pid.data(), pid.size());
  return true;
}

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
  algocraft::SqliteDatabase db_write;
  algocraft::SqliteDatabase db_read;
  algocraft::RocksBarStore bars;
  std::optional<algocraft::CoverageRepository> coverage;
  std::optional<algocraft::PersistenceService> persist;
  // CLI helpers (instruments ingest, etc.) still write on the RW handle.
  algocraft::SqliteDatabase& db;

  EngineCache()
      : db_write(engine_persistence_cfg()),
        db_read(engine_persistence_cfg()),
        bars(ALGOCRAFT_BARS_DIR),
        db(db_write) {
    db_write.open();
    db_write.migrate();
    db_read.open_readonly();
    bars.open();
    coverage.emplace(db_read.handle());
    if (algocraft::LogHub* hub = algocraft::log::hub()) {
      persist.emplace(db_write, *hub);
      persist->start();
    }
    AC_LOG_INFO("sqlite_write={} sqlite_read={} rocksdb={}", db_write.path().string(),
                db_read.path().string(), bars.path().string());
  }

  ~EngineCache() {
    if (persist) {
      persist->stop();
    }
  }

  algocraft::CoverageRepository& cov() { return *coverage; }
  [[nodiscard]] algocraft::PersistenceService* persist_ptr() {
    return persist ? &*persist : nullptr;
  }
};

std::unique_ptr<algocraft::CachedProvider> wrap_csv(const char* data_dir,
                                                    algocraft::SymbolTable& symbols,
                                                    EngineCache& cache) {
  return std::make_unique<algocraft::CachedProvider>(
      std::make_unique<algocraft::CsvProvider>(data_dir, &symbols), cache.bars, cache.cov(),
      symbols, cache.persist_ptr());
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
  AC_LOG_INFO("sqlite path={} wal={} tables={} migrations={}", db.path().string(),
               db.journal_mode(), tables.size(), applied.size());
  for (const auto& name : applied) {
    AC_LOG_INFO("  applied {}", name);
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
  AC_LOG_INFO("instruments ingest source={}", source);
  const auto stats = algocraft::ingest_upstox_instruments(repo, source);
  AC_LOG_INFO("instruments read={} kept={} skipped={} active={}", stats.rows_read,
               stats.rows_kept, stats.rows_skipped, repo.count_active());
  db.close();
  return 0;
}

int run_phase0_smoke() {
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::make_unique<algocraft::DummyProvider>());
  AC_LOG_INFO("active data source: {}", registry.active_provider().name());

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
  AC_LOG_INFO("bars={} fills={} commands={} persist={} logs={}", runtime.bars_processed(),
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

  AC_LOG_INFO("consecutive_up_clip capital=2cr/stock clip=20L data={}", data_dir);
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
    AC_LOG_INFO(
        "{} bars={} fills={} trips={} pnl_rupees={:.2f} fees_rupees={:.2f} return_pct={:.4f} "
        "sharpe={:.4f} max_dd_rupees={:.2f} win_rate={:.3f}",
        tickers[i], r.bars, r.fills, r.round_trips, r.realized_pnl_paise / 100.0,
        r.fees_paise / 100.0, ret_pct, r.sharpe, r.max_drawdown_paise / 100.0, r.win_rate);

    std::size_t fill_i = 0;
    for (const auto& day : r.daily) {
      const auto d = ist_tm(day.timestamp_ns);
      AC_LOG_INFO("  {:04d}-{:02d}-{:02d} pnl_rs={:.2f} fees_rs={:.2f} fills={} trips={} wins={}",
                   d.tm_year + 1900, d.tm_mon + 1, d.tm_mday, day.realized_pnl_paise / 100.0,
                   day.fees_paise / 100.0, day.fills, day.round_trips, day.wins);
      const auto day_key = day.timestamp_ns / 86'400'000'000'000LL;
      while (fill_i < r.fills_log.size() &&
             r.fills_log[fill_i].timestamp_ns / 86'400'000'000'000LL == day_key) {
        const auto& f = r.fills_log[fill_i];
        const auto t = ist_tm(f.timestamp_ns);
        AC_LOG_INFO("    {:02d}:{:02d} {} qty={} px={:.2f} fees={:.2f}", t.tm_hour, t.tm_min,
                     f.side == algocraft::Side::Buy ? "BUY " : "SELL", f.qty, f.price_paise / 100.0,
                     f.fees_paise / 100.0);
        ++fill_i;
      }
    }
  }
  AC_LOG_INFO("vendor_fetches={}", fetch->vendor_fetches());
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
  AC_LOG_INFO("active data source: {} dir={}", registry.active_provider().name(), data_dir);

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
      AC_LOG_INFO(
          "{} {} bars={} fills={} trips={} pnl_paise={} fees_paise={} equity_paise={} "
          "return_pct={:.4f} sharpe={:.4f} max_dd_paise={} win_rate={:.3f} avg_hold_s={:.1f}",
          r.strategy_name, tickers[i], r.bars, r.fills, r.round_trips, r.realized_pnl_paise,
          r.fees_paise, r.ending_equity_paise, ret_pct, r.sharpe, r.max_drawdown_paise, r.win_rate,
          r.avg_hold_seconds);
    }
  }
  AC_LOG_INFO("vendor_fetches={}", fetch->vendor_fetches());
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

  AC_LOG_INFO(
      "phase4 default_router 10 stocks x ema/vwap/clip, eval=14 sessions "
      "2026-08-24..2026-09-10, trade=2026-09-11, capital=10cr data={}",
      data_dir);
  algocraft::RunManager mgr;
  const auto result =
      mgr.execute(cfg, registry, strategies, books, symbols, nullptr, cache.persist_ptr());

  std::int64_t eval_pnl = 0;
  AC_LOG_INFO("--- 14-day eval (₹10L/pair) selected={} skipped={}", result.selected,
               result.skipped);
  for (const auto& ev : result.evaluations) {
    eval_pnl += ev.pnl_paise;
    AC_LOG_INFO("  {} {} bars={} fills={} pnl_rs={:.2f} {}", ev.ticker, ev.strategy_name, ev.bars,
                 ev.fills, ev.pnl_paise / 100.0, ev.selected ? "WINNER" : "SKIP");
  }
  AC_LOG_INFO("  eval_total_pnl_rs={:.2f}", eval_pnl / 100.0);

  const auto trade_pnl = result.returned.paise() - cfg.workbook_capital.paise();
  AC_LOG_INFO(
      "--- 15th day REAL 2026-09-11 containers={} fills={} trade_pnl_rs={:.2f} "
      "returned_rs={:.2f} workbook_after_rs={:.2f} force_stop={}",
      result.real_containers, result.fills, trade_pnl / 100.0, result.returned.paise() / 100.0,
      result.workbook_available_after.paise() / 100.0, result.force_stopped);
  for (const auto& row : result.traded) {
    AC_LOG_INFO("  {} {} alloc_rs={:.2f} realized_rs={:.2f} cash_rs={:.2f} fills={}", row.ticker,
                 row.strategy_name, row.allocation.paise() / 100.0, row.realized.paise() / 100.0,
                 row.cash.paise() / 100.0, row.fills);
  }
  AC_LOG_INFO("vendor_fetches={}", fetch->vendor_fetches());
  return 0;
}

int run_api_server(const char* data_dir, int port) {
  // Refuse before RocksDB: only one engine may own data/bars.
  if (other_serve_running()) {
    AC_LOG_ERROR(
        "another algocraft_engine is already running — RocksDB lock would fail.\n{}"
        "Fix:  scripts/serve --restart",
        list_algocraft_engine_procs());
    return 1;
  }

  const std::filesystem::path serve_lock{"data/serve.lock"};
  if (!acquire_serve_lock(serve_lock)) {
    AC_LOG_ERROR(
        "could not acquire {} (another serve starting?).\n{}"
        "Fix:  scripts/serve --restart",
        serve_lock.string(), list_algocraft_engine_procs());
    return 1;
  }

  try {
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
                                                           symbols, cache.persist_ptr());
      AC_LOG_INFO("data source: upstox (token loaded from ~/.config/upstox/config.json)");
    } else {
      cached = wrap_csv(data_dir, symbols, cache);
      AC_LOG_WARN("upstox token missing — serving CSV from {}", data_dir);
    }
    fetch_ptr = &cached->fetch();

    algocraft::DataSourceRegistry registry;
    registry.register_provider(std::move(cached));

    algocraft::StrategyRegistry strategies;
    algocraft::register_all_strategies(strategies);

    algocraft::HttpServer::Config cfg;
    cfg.host = "127.0.0.1";
    cfg.port = port;
    algocraft::HttpServer server(cfg, cache.db_read, *cache.persist, registry, strategies, symbols,
                                 fetch_ptr);
    AC_LOG_INFO("API listening on http://{}:{}", cfg.host, cfg.port);
    server.start();
    return 0;
  } catch (const std::exception& e) {
    AC_LOG_ERROR("serve failed: {}", e.what());
    return 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto peeled = peel_log_level(argc, argv, 1);
  const algocraft::LogLevel log_level = peeled.level.value_or(algocraft::LogLevel::Info);

  algocraft::LogHub log_hub;
  algocraft::log::init_sinks("data/logs");
  algocraft::log::set_level(log_level);
  algocraft::log::set_hub(&log_hub);

  int rc = 0;
  const char* cmd = (argc >= 2) ? argv[1] : "";

  // EngineCache (serve/run/backtest) owns log drain via PersistenceService (T2).
  // Other commands keep a dedicated log_drain thread.
  const bool persist_owns_logs = std::strcmp(cmd, "serve") == 0 || std::strcmp(cmd, "run") == 0 ||
                                 std::strcmp(cmd, "backtest") == 0;
  if (!persist_owns_logs) {
    log_hub.start();
  }

  AC_LOG_INFO("algocraft starting log_level={} log_dir={}", algocraft::to_string(log_level),
              algocraft::log::log_dir());

  // Re-peel after the command name so `serve debug` works (level was also in first peel).
  const auto cmd_args = peel_log_level(argc, argv, 2);

  if (argc >= 2 && std::strcmp(cmd, "db") == 0) {
    const char* db_path = arg_or(cmd_args, 0, ALGOCRAFT_DB_PATH);
    rc = run_db_smoke(db_path);
  } else if (argc >= 2 && std::strcmp(cmd, "instruments") == 0) {
    if (cmd_args.rest.size() >= 1 && std::strcmp(cmd_args.rest[0], "ingest") == 0) {
      const char* source = arg_or(cmd_args, 1, algocraft::kUpstoxInstrumentsUrl.data());
      const char* db_path = arg_or(cmd_args, 2, ALGOCRAFT_DB_PATH);
      rc = run_instruments_ingest(source, db_path);
    } else {
      AC_LOG_ERROR("usage: algocraft_engine instruments ingest [source] [db_path] [log_level]");
      rc = 1;
    }
  } else if (argc >= 2 && std::strcmp(cmd, "serve") == 0) {
    const char* data_dir = arg_or(cmd_args, 0, ALGOCRAFT_DATA_DIR);
    int port = 8080;
    if (cmd_args.rest.size() >= 2) {
      port = std::atoi(cmd_args.rest[1]);
    }
    rc = run_api_server(data_dir, port);
  } else if (argc >= 2 && std::strcmp(cmd, "run") == 0) {
    const char* data_dir = arg_or(cmd_args, 0, ALGOCRAFT_DATA_DIR);
    rc = run_phase4(data_dir);
  } else if (argc >= 2 && std::strcmp(cmd, "backtest") == 0) {
    if (cmd_args.rest.size() >= 1 && std::strcmp(cmd_args.rest[0], "consecutive_up_clip") == 0) {
      const char* data_dir = arg_or(cmd_args, 1, ALGOCRAFT_DATA_DIR);
      rc = run_clip_backtest(data_dir);
    } else {
      const char* data_dir = arg_or(cmd_args, 0, ALGOCRAFT_DATA_DIR);
      rc = run_phase2_backtest(data_dir);
    }
  } else {
    rc = run_phase0_smoke();
  }

  AC_LOG_INFO("algocraft exit rc={} log_dropped={} log_written={}", rc, log_hub.dropped(),
              log_hub.written());
  algocraft::log::set_hub(nullptr);
  log_hub.stop();
  return rc;
}
