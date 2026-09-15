#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/engine/phase0_runtime.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/dummy_provider.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#ifndef ALGOCRAFT_DATA_DIR
#define ALGOCRAFT_DATA_DIR "data/1min"
#endif

namespace {

std::tm ist_tm(std::int64_t timestamp_ns) {
  const auto ist_sec = static_cast<std::time_t>(timestamp_ns / 1'000'000'000LL + 19800);
  std::tm out{};
  gmtime_r(&ist_sec, &out);
  return out;
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

  auto csv = std::make_unique<algocraft::CsvProvider>(data_dir, &symbols);
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(csv));

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

  auto csv = std::make_unique<algocraft::CsvProvider>(data_dir, &symbols);
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(csv));
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
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
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
