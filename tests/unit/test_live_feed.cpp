#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/session_calendar.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/domain/session_date.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/engine/live_run_service.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/market_data/csv_provider.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/market_data/market_data_feed.hpp"
#include "algocraft/persistence/activity_repository.hpp"
#include "algocraft/persistence/persistence_config.hpp"
#include "algocraft/persistence/sqlite_database.hpp"
#include "algocraft/persistence/workbook_repository.hpp"
#include "algocraft/routing/default_router.hpp"
#include "algocraft/strategies/make_intent.hpp"
#include "algocraft/strategies/strategy.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

#ifndef ALGOCRAFT_MIGRATIONS_DIR
#define ALGOCRAFT_MIGRATIONS_DIR "migrations"
#endif

namespace {

using algocraft::BarEvent;
using algocraft::Capital;
using algocraft::CsvProvider;
using algocraft::DataSourceRegistry;
using algocraft::IndicatorLibrary;
using algocraft::LiveRunService;
using algocraft::MarketDataFeed;
using algocraft::MinuteBarBuilder;
using algocraft::PortfolioView;
using algocraft::Price;
using algocraft::Quantity;
using algocraft::RunConfig;
using algocraft::SessionDate;
using algocraft::Side;
using algocraft::Strategy;
using algocraft::StrategyConfig;
using algocraft::StrategyId;
using algocraft::StrategyMetadata;
using algocraft::StrategyRegistry;
using algocraft::SymbolId;
using algocraft::SymbolTable;
using algocraft::Timestamp;
using algocraft::UserId;
using algocraft::WorkbookId;
using algocraft::WorkbookManager;

// In-process ticks for unit tests (no Upstox).
class ScriptedLiveFeed final : public MarketDataFeed {
public:
  void connect() override { connected_ = true; }
  void disconnect() override { connected_ = false; }
  void subscribe(SymbolId symbol_id) override { subscribed_.insert(symbol_id); }
  void unsubscribe(SymbolId symbol_id) override { subscribed_.erase(symbol_id); }

  void push_tick(SymbolId symbol_id, Price ltp, Timestamp ts) {
    if (!connected_ || !subscribed_.contains(symbol_id)) {
      return;
    }
    emit_tick(symbol_id, ltp, ts);
  }

private:
  bool connected_{false};
  std::unordered_set<SymbolId> subscribed_{};
};

Timestamp ist_clock(int y, unsigned m, unsigned d, int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{y} / month{m} / day{d}} + hours{10} + minutes{minute} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

void write_trend_csv(const std::filesystem::path& path, int start_rupees) {
  std::ofstream out(path);
  out << "timestamp_ns,open,high,low,close,volume\n";
  for (int i = 0; i < 12; ++i) {
    const auto px = start_rupees + i;
    const auto ns = ist_clock(2026, 8, 28, i).nanos();
    out << ns << ',' << px << ".00," << px << ".00," << px << ".00," << px << ".00,1000\n";
  }
}

class ProfitThenFlat final : public Strategy {
public:
  void configure(const StrategyConfig& config, IndicatorLibrary&) override { config_ = config; }
  void on_bar(const BarEvent&, const PortfolioView& portfolio,
              std::vector<algocraft::OrderIntent>& out) override {
    ++bars_;
    if (bars_ == 1 && portfolio.position.shares() == 0) {
      out.push_back(algocraft::make_intent(StrategyId::from(1), config_.symbol_id, Side::Buy,
                                           config_.order_qty));
    }
    if (bars_ == 3 && portfolio.position.shares() > 0) {
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

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_live" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

struct LiveFixture {
  std::filesystem::path dir;
  algocraft::SqliteDatabase db;
  std::optional<algocraft::WorkbookRepository> workbooks;
  std::optional<algocraft::ActivityRepository> activity;

  LiveFixture()
      : dir(make_temp_dir()),
        db([](const std::filesystem::path& d) {
          algocraft::PersistenceConfig c;
          c.db_path = d / "algocraft.db";
          c.migrations_dir = ALGOCRAFT_MIGRATIONS_DIR;
          c.bars_dir = d / "bars";
          return c;
        }(dir)) {
    db.open();
    db.migrate();
    workbooks.emplace(db.handle());
    activity.emplace(db.handle());
  }

  ~LiveFixture() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

}  // namespace

TEST(MinuteBarBuilder, EmitsClosedMinuteOnRoll) {
  std::vector<BarEvent> bars;
  algocraft::MinuteBarBuilder builder([&](const BarEvent& b) { bars.push_back(b); });

  const auto t0 = ist_clock(2026, 9, 18, 0);  // 10:00 IST bucket via helper
  const auto t1 = Timestamp::from_nanos(t0.nanos() + algocraft::kNanosPerMinute / 2);
  const auto t2 = Timestamp::from_nanos(t0.nanos() + algocraft::kNanosPerMinute);

  builder.on_tick(SymbolId{1}, Price::from_paise(100'00), t0);
  builder.on_tick(SymbolId{1}, Price::from_paise(101'00), t1);
  EXPECT_TRUE(bars.empty());
  builder.on_tick(SymbolId{1}, Price::from_paise(102'00), t2);
  ASSERT_EQ(bars.size(), 1u);
  EXPECT_EQ(bars[0].open.paise(), 100'00);
  EXPECT_EQ(bars[0].high.paise(), 101'00);
  EXPECT_EQ(bars[0].close.paise(), 101'00);
  builder.flush();
  ASSERT_EQ(bars.size(), 2u);
  EXPECT_EQ(bars[1].close.paise(), 102'00);
}

TEST(LiveAnchor, TodayIsLivePastIsNot) {
  const auto now = Timestamp::now();
  const auto today = SessionDate::from_ist(now);
  EXPECT_TRUE(algocraft::is_live_anchor(today, now));
  EXPECT_FALSE(algocraft::is_live_anchor(algocraft::prev_session_day(today), now));
}

TEST(ScriptedLiveFeed, MinuteBarsDriveLiveRunSettle) {
  LiveFixture fx;
  write_trend_csv(fx.dir / "AAA.csv", 100);

  SymbolTable symbols;
  auto csv = std::make_unique<CsvProvider>(fx.dir, &symbols);
  DataSourceRegistry registry;
  registry.register_provider(std::move(csv));

  StrategyRegistry strategies;
  strategies.add("profit", [] { return std::make_unique<ProfitThenFlat>(); });

  WorkbookManager books;
  const auto user = UserId::from_u64(1);
  const auto capital = Capital::from_paise(10'00'000'00);
  const auto wid = fx.workbooks->create(1, "live-test", capital.paise(), "t", "user");
  const auto wb = WorkbookId::from_u64(static_cast<std::uint64_t>(wid));
  ASSERT_TRUE(books.adopt(wb, user, "live-test", capital, capital).ok);

  ScriptedLiveFeed feed;
  LiveRunService live({
      .data = registry,
      .strategies = strategies,
      .symbols = symbols,
      .activity = *fx.activity,
      .workbooks = *fx.workbooks,
      .books = books,
      .hub = nullptr,
  });
  live.set_ignore_session_end(true);
  live.set_feed_override(&feed);

  RunConfig cfg{};
  cfg.user_id = user;
  cfg.workbook_name = "live-test";
  cfg.workbook_capital = capital;
  cfg.existing_workbook_id = wb;
  cfg.tickers = {"AAA"};
  cfg.strategies = {"profit"};
  cfg.router = "default_router";
  cfg.from = ist_clock(2026, 8, 28, 0);
  cfg.to = ist_clock(2026, 8, 28, 20);
  cfg.anchor_date = SessionDate::from_ist(Timestamp::now());

  const auto started = live.start(cfg);
  ASSERT_TRUE(started.ok) << started.error;
  ASSERT_TRUE(started.live_started);
  EXPECT_GE(started.result.selected, 1);
  EXPECT_TRUE(live.is_running(wid));

  const auto id = symbols.find("AAA");
  ASSERT_TRUE(id.has_value());
  // Drive two closed minutes + flush on stop.
  const auto base = ist_clock(2026, 9, 18, 15);
  feed.push_tick(*id, Price::from_paise(150'00), base);
  feed.push_tick(*id, Price::from_paise(151'00),
                 Timestamp::from_nanos(base.nanos() + algocraft::kNanosPerMinute));
  feed.push_tick(*id, Price::from_paise(152'00),
                 Timestamp::from_nanos(base.nanos() + 2 * algocraft::kNanosPerMinute));

  ASSERT_TRUE(live.stop(wid));
  EXPECT_FALSE(live.is_running(wid));
  EXPECT_EQ(books.get_workbook(wb)->borrowed_capital.paise(), 0);
  const auto runs = fx.activity->list_runs(wid);
  ASSERT_FALSE(runs.empty());
}

TEST(DefaultRouter, DefaultsSupplyUniverse) {
  algocraft::DefaultRouter router;
  const auto d = router.defaults();
  EXPECT_EQ(d.eval_sessions, 14);
  EXPECT_GE(d.tickers.size(), 3u);
  EXPECT_GE(d.strategies.size(), 1u);
  EXPECT_NE(std::find(d.tickers.begin(), d.tickers.end(), "RELIANCE"), d.tickers.end());
  EXPECT_NE(std::find(d.strategies.begin(), d.strategies.end(), "ema_crossover"),
            d.strategies.end());
}
