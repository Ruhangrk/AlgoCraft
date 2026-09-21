#include "algocraft/market_data/upstox_provider.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <gtest/gtest.h>

#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/symbol.hpp"

namespace {

algocraft::Timestamp ist_midnight(int y, int m, int d) {
  using namespace std::chrono;
  const auto utc = sys_days{year{y} / m / d} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

}  // namespace

TEST(UpstoxHistoryUnit, MapsChartAndEngineResolutions) {
  const auto m1 = algocraft::upstox_history_unit(algocraft::BarResolution::OneMin);
  EXPECT_EQ(m1.unit, "minutes");
  EXPECT_EQ(m1.interval, 1);
  EXPECT_EQ(m1.max_window_calendar_days, 28);

  const auto d1 = algocraft::upstox_history_unit(algocraft::BarResolution::OneDay);
  EXPECT_EQ(d1.unit, "days");
  EXPECT_EQ(d1.interval, 1);
  EXPECT_EQ(d1.max_window_calendar_days, 9 * 365);

  const auto w1 = algocraft::upstox_history_unit(algocraft::BarResolution::OneWeek);
  EXPECT_EQ(w1.unit, "weeks");

  const auto mo = algocraft::upstox_history_unit(algocraft::BarResolution::OneMonth);
  EXPECT_EQ(mo.unit, "months");

  EXPECT_THROW(
      static_cast<void>(algocraft::upstox_history_unit(algocraft::BarResolution::FiveMin)),
      std::runtime_error);
}

TEST(UpstoxChunkRange, SplitsInclusiveWindows) {
  const auto from = ist_midnight(2026, 1, 1);
  const auto to = ist_midnight(2026, 1, 10);  // 10 calendar days inclusive
  const auto chunks = algocraft::upstox_chunk_range(from, to, 4);
  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_EQ(chunks[0].first.nanos(), from.nanos());
  EXPECT_EQ(chunks[0].second.nanos(), ist_midnight(2026, 1, 4).nanos());
  EXPECT_EQ(chunks[1].first.nanos(), ist_midnight(2026, 1, 5).nanos());
  EXPECT_EQ(chunks[1].second.nanos(), ist_midnight(2026, 1, 8).nanos());
  EXPECT_EQ(chunks[2].first.nanos(), ist_midnight(2026, 1, 9).nanos());
  EXPECT_EQ(chunks[2].second.nanos(), to.nanos());
}

TEST(UpstoxChunkRange, SingleChunkWhenWithinCap) {
  const auto from = ist_midnight(2026, 9, 10);
  const auto to = ist_midnight(2026, 9, 11);
  const auto chunks = algocraft::upstox_chunk_range(from, to, 28);
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].first.nanos(), from.nanos());
  EXPECT_EQ(chunks[0].second.nanos(), to.nanos());
}

TEST(UpstoxProvider, CapabilitiesIncludeChartResolutions) {
  algocraft::UpstoxConfig cfg;
  cfg.access_token = "test";
  algocraft::UpstoxProvider provider(cfg);
  const auto caps = provider.capabilities();
  ASSERT_GE(caps.supported_resolutions.size(), 4u);
  EXPECT_NE(std::find(caps.supported_resolutions.begin(), caps.supported_resolutions.end(),
                      algocraft::BarResolution::OneDay),
            caps.supported_resolutions.end());
  EXPECT_NE(std::find(caps.supported_resolutions.begin(), caps.supported_resolutions.end(),
                      algocraft::BarResolution::OneMonth),
            caps.supported_resolutions.end());
}

// Live smoke: short daily range for one equity. Skips without token (no network blast).
TEST(UpstoxProvider, SmokeDailyShortRange) {
  auto cfg = algocraft::UpstoxConfig::from_default_file();
  if (!cfg.ok()) {
    GTEST_SKIP() << "no upstox token";
  }
  cfg.min_interval_ms = 2100;
  algocraft::SymbolTable symbols;
  const auto id = symbols.intern({.ticker = "RELIANCE"}, {});
  algocraft::UpstoxProvider provider(cfg, &symbols);
  const auto from = ist_midnight(2026, 9, 10);
  const auto to = ist_midnight(2026, 9, 11);
  const auto bars = provider.historical_loader().load_bars(id, from, to,
                                                           algocraft::BarResolution::OneDay);
  ASSERT_FALSE(bars.empty());
  EXPECT_EQ(bars.front().resolution, algocraft::BarResolution::OneDay);
  EXPECT_GE(provider.upstox_loader().http_calls(), 1u);
}
