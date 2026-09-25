#include "algocraft/backtest/backtest_runner.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/strategies/strategy_registry.hpp"

#include <array>
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#ifndef ALGOCRAFT_TEST_DATA_DIR
#define ALGOCRAFT_TEST_DATA_DIR "."
#endif

#ifndef ALGOCRAFT_DATA_DIR
#define ALGOCRAFT_DATA_DIR "."
#endif

namespace {

algocraft::BacktestResult run_named(const char* csv_file, const char* strategy_name,
                                    algocraft::StrategyConfig cfg) {
  auto csv = std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_TEST_DATA_DIR);
  csv->csv_loader().set_file(1, std::filesystem::path(ALGOCRAFT_TEST_DATA_DIR) / csv_file);

  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(csv));

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  algocraft::BacktestRequest req{};
  req.symbol_id = 1;
  req.strategy_name = strategy_name;
  req.strategy = cfg;
  req.starting_capital = algocraft::Capital::from_paise(10'00'000'00);

  algocraft::BacktestRunner runner;
  return runner.run(registry, strategies, req);
}

}  // namespace

TEST(CsvProvider, LoadsFixtureThroughRegistry) {
  auto csv = std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_TEST_DATA_DIR);
  csv->csv_loader().set_file(1, std::filesystem::path(ALGOCRAFT_TEST_DATA_DIR) / "orb.csv");
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(csv));
  EXPECT_EQ(registry.active_provider().name(), "csv");
  EXPECT_EQ(registry.active_provider().live_feed(), nullptr);

  const auto bars = registry.active_provider().historical_loader().load_bars(
      1, {}, {}, algocraft::BarResolution::OneMin);
  ASSERT_EQ(bars.size(), 5u);
  EXPECT_EQ(bars[0].close.paise(), 10000);
}

TEST(Backtest, EmaCrossoverIsDeterministic) {
  algocraft::StrategyConfig cfg{};
  cfg.ema_fast = 2;
  cfg.ema_slow = 3;
  cfg.order_qty = algocraft::Quantity::from_shares(1);
  const auto a = run_named("ema_cross.csv", "ema_crossover", cfg);
  const auto b = run_named("ema_cross.csv", "ema_crossover", cfg);
  EXPECT_EQ(a.fills, b.fills);
  EXPECT_EQ(a.ending_equity_paise, b.ending_equity_paise);
  EXPECT_EQ(a.realized_pnl_paise, b.realized_pnl_paise);
  EXPECT_EQ(a.max_drawdown_paise, b.max_drawdown_paise);
  EXPECT_GT(a.fills, 0);
}

TEST(Backtest, AllThreeStrategiesRunOnFixtures) {
  algocraft::StrategyConfig ema{};
  ema.ema_fast = 2;
  ema.ema_slow = 3;
  EXPECT_GT(run_named("ema_cross.csv", "ema_crossover", ema).fills, 0);

  algocraft::StrategyConfig vwap{};
  vwap.vwap_dev_paise = 50;
  EXPECT_GE(run_named("vwap_rev.csv", "vwap_reversion", vwap).fills, 0);

  algocraft::StrategyConfig orb{};
  orb.orb_bars = 3;
  const auto orb_result = run_named("orb.csv", "opening_range_breakout", orb);
  EXPECT_GE(orb_result.fills, 1);
}

TEST(StrategyRegistry, ListsBuiltIns) {
  algocraft::StrategyRegistry registry;
  algocraft::register_all_strategies(registry);
  const auto names = registry.names();
  EXPECT_EQ(names.size(), 5u);
  const auto ema = registry.create("ema_crossover");
  ASSERT_FALSE(ema->metadata().required_indicators.empty());
}

TEST(Backtest, ThreeStrategiesOnNseCsvAreDeterministic) {
  algocraft::SymbolTable symbols;
  const algocraft::Instrument inst{};
  const std::array<const char*, 3> tickers{"RELIANCE", "INFY", "TCS"};
  std::array<algocraft::SymbolId, 3> ids{};
  for (std::size_t i = 0; i < tickers.size(); ++i) {
    ids[i] = symbols.intern({.ticker = tickers[i]}, inst);
  }

  auto csv = std::make_unique<algocraft::CsvProvider>(ALGOCRAFT_DATA_DIR, &symbols);
  algocraft::DataSourceRegistry registry;
  registry.register_provider(std::move(csv));
  EXPECT_EQ(registry.active_provider().name(), "csv");

  algocraft::StrategyRegistry strategies;
  algocraft::register_all_strategies(strategies);

  const std::array<const char*, 3> names{"ema_crossover", "vwap_reversion",
                                         "opening_range_breakout"};
  algocraft::BacktestRunner runner;
  for (const char* strategy_name : names) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
      algocraft::BacktestRequest req{};
      req.symbol_id = ids[i];
      req.strategy_name = strategy_name;
      req.starting_capital = algocraft::Capital::from_paise(10'00'000'00);
      req.strategy.order_qty = algocraft::Quantity::from_shares(10);
      const auto a = runner.run(registry, strategies, req);
      const auto b = runner.run(registry, strategies, req);
      EXPECT_GE(a.bars, 1000u) << strategy_name << " " << tickers[i];
      EXPECT_EQ(a.fills, b.fills);
      EXPECT_EQ(a.ending_equity_paise, b.ending_equity_paise);
      EXPECT_EQ(a.realized_pnl_paise, b.realized_pnl_paise);
      EXPECT_EQ(a.sharpe, b.sharpe);
    }
  }
}
