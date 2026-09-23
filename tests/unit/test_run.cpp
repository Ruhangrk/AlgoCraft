#include "algocraft/engine/run_manager.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include "algocraft/strategies/strategy.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

using algocraft::BarEvent;
using algocraft::Capital;
using algocraft::ContainerMode;
using algocraft::CsvProvider;
using algocraft::DataSourceRegistry;
using algocraft::IndicatorLibrary;
using algocraft::PortfolioView;
using algocraft::Price;
using algocraft::Quantity;
using algocraft::RunConfig;
using algocraft::RunManager;
using algocraft::Side;
using algocraft::Strategy;
using algocraft::StrategyConfig;
using algocraft::StrategyId;
using algocraft::StrategyMetadata;
using algocraft::StrategyRegistry;
using algocraft::SymbolTable;
using algocraft::UserId;
using algocraft::WorkbookManager;

namespace {

algocraft::Timestamp ist_clock(int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{2026} / 8 / 28} + hours{10} + minutes{minute} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

void write_trend_csv(const std::filesystem::path& path, int start_rupees) {
  std::ofstream out(path);
  out << "timestamp_ns,open,high,low,close,volume\n";
  for (int i = 0; i < 12; ++i) {
    const auto px = start_rupees + i;
    const auto ns = ist_clock(i).nanos();
    out << ns << ',' << px << ".00," << px << ".00," << px << ".00," << px << ".00,1000\n";
  }
}

class ProfitThenFlat final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary&) override { config_ = config; }
  void on_bar(const BarEvent&, const PortfolioView& portfolio,
              std::vector<algocraft::OrderIntent>& out) override {
    ++bars_;
    if (bars_ == 2 && portfolio.position.shares() == 0) {
      out.push_back(algocraft::make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy,
                                           config_.order_qty));
    }
    if (bars_ == 8 && portfolio.position.shares() > 0) {
      out.push_back(algocraft::make_intent(StrategyId::from(1), config_.symbol_id, Side::Sell,
                                           portfolio.position));
    }
  }
  void on_fill(const algocraft::FillEvent&) override {}
  void on_order_update(const algocraft::OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override {
    return {"profit", "1.0.0", algocraft::TradingMode::Mis, algocraft::BarResolution::OneMin, {}};
  }

private:
  StrategyConfig config_{};
  int bars_{0};
};

class NeverTrade final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override {}
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<algocraft::OrderIntent>&) override {
  }
  void on_fill(const algocraft::FillEvent&) override {}
  void on_order_update(const algocraft::OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override {
    return {"never", "1.0.0", algocraft::TradingMode::Mis, algocraft::BarResolution::OneMin, {}};
  }
};

std::filesystem::path fixture_dir() {
  const auto dir = std::filesystem::temp_directory_path() / "algocraft_phase4";
  std::filesystem::create_directories(dir);
  write_trend_csv(dir / "AAA.csv", 100);
  write_trend_csv(dir / "BBB.csv", 200);
  return dir;
}

}  // namespace

TEST(RunManager, SkipsLosersPromotesWinnersToReal) {
  const auto dir = fixture_dir();
  SymbolTable symbols;
  auto csv = std::make_unique<CsvProvider>(dir, &symbols);
  DataSourceRegistry registry;
  registry.register_provider(std::move(csv));

  StrategyRegistry strategies;
  strategies.add("profit", [] { return std::make_unique<ProfitThenFlat>(); });
  strategies.add("never", [] { return std::make_unique<NeverTrade>(); });

  WorkbookManager books;
  RunConfig cfg{};
  cfg.user_id = UserId::from_u64(1);
  cfg.workbook_capital = Capital::from_paise(10'00'000'00);
  cfg.tickers = {"AAA", "BBB"};
  cfg.strategies = {"profit", "never"};
  cfg.from = ist_clock(0);
  cfg.to = ist_clock(20);

  RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols);
  EXPECT_EQ(result.evaluations.size(), 4u);
  EXPECT_EQ(result.selected, 2);
  EXPECT_EQ(result.skipped, 2);
  EXPECT_EQ(result.real_containers, 2);
  EXPECT_EQ(result.workbook_available_before.paise(), 10'00'000'00);
  EXPECT_EQ(books.get_workbook(result.workbook_id)->borrowed_capital.paise(), 0);
  EXPECT_GT(result.fills, 0);
  for (const auto& ev : result.evaluations) {
    if (ev.strategy_name == "profit") {
      EXPECT_TRUE(ev.selected);
      EXPECT_GT(ev.pnl_paise, 0);
    } else {
      EXPECT_FALSE(ev.selected);
    }
  }
}

TEST(RunManager, ForceStopReturnsCapital) {
  const auto dir = fixture_dir();
  SymbolTable symbols;
  auto csv = std::make_unique<CsvProvider>(dir, &symbols);
  DataSourceRegistry registry;
  registry.register_provider(std::move(csv));

  StrategyRegistry strategies;
  strategies.add("profit", [] { return std::make_unique<ProfitThenFlat>(); });
  strategies.add("never", [] { return std::make_unique<NeverTrade>(); });

  WorkbookManager books;
  RunConfig cfg{};
  cfg.user_id = UserId::from_u64(2);
  cfg.workbook_capital = Capital::from_paise(10'00'000'00);
  cfg.tickers = {"AAA"};
  cfg.strategies = {"profit", "never"};
  cfg.from = ist_clock(0);
  cfg.to = ist_clock(20);
  cfg.max_replay_bars = 3;

  RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols);
  EXPECT_TRUE(result.force_stopped);
  EXPECT_EQ(result.selected, 1);
  EXPECT_EQ(books.get_workbook(result.workbook_id)->borrowed_capital.paise(), 0);
  EXPECT_EQ(books.activity(result.borrow_id)->settled(), true);
}

TEST(RunManager, EmptySkipReturnsFullCapital) {
  const auto dir = fixture_dir();
  SymbolTable symbols;
  auto csv = std::make_unique<CsvProvider>(dir, &symbols);
  DataSourceRegistry registry;
  registry.register_provider(std::move(csv));

  StrategyRegistry strategies;
  strategies.add("never", [] { return std::make_unique<NeverTrade>(); });

  WorkbookManager books;
  RunConfig cfg{};
  cfg.user_id = UserId::from_u64(3);
  cfg.workbook_capital = Capital::from_paise(10'00'00'000'00);
  cfg.tickers = {"AAA", "BBB"};
  cfg.strategies = {"never"};
  cfg.from = ist_clock(0);
  cfg.to = ist_clock(20);

  RunManager mgr;
  const auto result = mgr.execute(cfg, registry, strategies, books, symbols);
  EXPECT_EQ(result.selected, 0);
  EXPECT_EQ(result.real_containers, 0);
  EXPECT_EQ(result.workbook_available_after.paise(), 10'00'00'000'00);
}

TEST(RunWindows, ParseHhmm) {
  EXPECT_EQ(algocraft::parse_hhmm("09:15"), 9 * 60 + 15);
  EXPECT_EQ(algocraft::parse_hhmm("15:30"), 15 * 60 + 30);
  EXPECT_FALSE(algocraft::parse_hhmm("9:15").has_value());
  EXPECT_FALSE(algocraft::parse_hhmm("25:00").has_value());
}

TEST(RunWindows, AnchorShiftsEvalBeforeTradeDay) {
  const auto anchor = algocraft::SessionDate::from_iso("2026-09-18");
  const auto w = algocraft::resolve_anchor_run_windows(anchor, 3, {}, {});

  EXPECT_EQ(w.anchor.iso(), "2026-09-18");
  EXPECT_EQ(w.eval_last.iso(), "2026-09-17");
  EXPECT_EQ(w.eval_first.iso(), "2026-09-15");
  EXPECT_EQ(w.eval_sessions, 3);

  EXPECT_EQ(algocraft::SessionDate::from_ist(w.trade_from).iso(), "2026-09-18");
  EXPECT_EQ(algocraft::SessionDate::from_ist(w.trade_to).iso(), "2026-09-18");
  EXPECT_LT(w.eval_to.nanos(), w.trade_from.nanos());
}

TEST(RunWindows, YesterdayVsTodayShiftsEvalByOneSession) {
  const auto today = algocraft::SessionDate::from_iso("2026-09-18");
  const auto yesterday = algocraft::SessionDate::from_iso("2026-09-17");
  const auto a = algocraft::resolve_anchor_run_windows(today, 2, {}, {});
  const auto b = algocraft::resolve_anchor_run_windows(yesterday, 2, {}, {});

  EXPECT_EQ(a.eval_last.iso(), "2026-09-17");
  EXPECT_EQ(b.eval_last.iso(), "2026-09-16");
  EXPECT_EQ(a.eval_first.iso(), "2026-09-16");
  EXPECT_EQ(b.eval_first.iso(), "2026-09-15");
}

TEST(RunWindows, IntradayTradeRange) {
  const auto anchor = algocraft::SessionDate::from_iso("2026-09-18");
  const auto w = algocraft::resolve_anchor_run_windows(anchor, 1, algocraft::parse_hhmm("09:15"),
                                                       algocraft::parse_hhmm("15:30"));
  EXPECT_EQ(algocraft::ist_minute_of_day(w.trade_from), 9 * 60 + 15);
  EXPECT_EQ(algocraft::ist_minute_of_day(w.trade_to), 15 * 60 + 30);
}

TEST(RunWindows, RejectsWeekendAnchor) {
  EXPECT_THROW(
      algocraft::resolve_anchor_run_windows(algocraft::SessionDate::from_iso("2026-09-19"), 5, {}, {}),
      std::invalid_argument);
}

TEST(RunWindows, RejectsBadEvalCount) {
  EXPECT_THROW(
      algocraft::resolve_anchor_run_windows(algocraft::SessionDate::from_iso("2026-09-18"), 0, {}, {}),
      std::invalid_argument);
}
