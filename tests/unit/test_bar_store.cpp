#include "algocraft/domain/session_calendar.hpp"
#include "algocraft/domain/session_date.hpp"
#include "algocraft/persistence/packed_bars.hpp"
#include "algocraft/persistence/rocks_bar_store.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto dir = std::filesystem::temp_directory_path() / "algocraft_bars" /
             (std::to_string(stamp) + "-" +
              std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
  std::filesystem::create_directories(dir);
  return dir;
}

algocraft::BarEvent sample_bar(algocraft::SymbolId id, std::int64_t ns, std::int64_t paise,
                               algocraft::BarResolution resolution = algocraft::BarResolution::OneMin) {
  algocraft::BarEvent bar{};
  bar.symbol_id = id;
  bar.timestamp = algocraft::Timestamp::from_nanos(ns);
  bar.resolution = resolution;
  bar.open = algocraft::Price::from_paise(paise);
  bar.high = algocraft::Price::from_paise(paise + 10);
  bar.low = algocraft::Price::from_paise(paise - 10);
  bar.close = algocraft::Price::from_paise(paise + 5);
  bar.volume = algocraft::Quantity::from_shares(1000);
  return bar;
}

}  // namespace

TEST(SessionDate, FromIstKnownRelianceOpen) {
  const auto d =
      algocraft::SessionDate::from_ist(algocraft::Timestamp::from_nanos(1'787'024'700'000'000'000LL));
  EXPECT_EQ(d.iso(), "2026-08-18");
  EXPECT_EQ(algocraft::weekday_sun0(d), 2);
  EXPECT_TRUE(algocraft::is_nse_session_day(d));
}

TEST(SessionCalendar, WeekendAndNextSession) {
  const auto friday = algocraft::SessionDate::from_iso("2026-08-28");
  EXPECT_TRUE(algocraft::is_nse_session_day(friday));
  EXPECT_TRUE(algocraft::is_weekend(algocraft::SessionDate::from_iso("2026-08-29")));
  EXPECT_TRUE(algocraft::is_weekend(algocraft::SessionDate::from_iso("2026-08-30")));
  EXPECT_EQ(algocraft::next_session_day(friday).iso(), "2026-08-31");
}

TEST(PackedBars, EmptyAndRoundTrip) {
  const auto empty = algocraft::pack_session_bars({});
  const auto decoded_empty = algocraft::unpack_session_bars(empty, 7, algocraft::BarResolution::OneMin);
  EXPECT_TRUE(decoded_empty.empty());

  std::vector<algocraft::BarEvent> bars;
  bars.push_back(sample_bar(7, 1'000, 131400));
  bars.push_back(sample_bar(7, 2'000, 131500));
  const auto blob = algocraft::pack_session_bars(bars);
  const auto out = algocraft::unpack_session_bars(blob, 9, algocraft::BarResolution::OneMin);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].symbol_id, 9u);
  EXPECT_EQ(out[0].timestamp.nanos(), 1'000);
  EXPECT_EQ(out[0].open.paise(), 131400);
  EXPECT_EQ(out[1].close.paise(), 131505);
  EXPECT_EQ(out[1].volume.shares(), 1000);
}

TEST(RocksBarStore, PutGetListAndEmptySession) {
  const auto dir = make_temp_dir();
  algocraft::RocksBarStore store(dir);
  store.open();
  EXPECT_TRUE(store.is_open());

  const auto day = algocraft::SessionDate::from_iso("2026-09-11");
  EXPECT_FALSE(store.has_session("RELIANCE", algocraft::BarResolution::OneMin, day));

  store.put_session("RELIANCE", algocraft::BarResolution::OneMin, day, {});
  ASSERT_TRUE(store.has_session("RELIANCE", algocraft::BarResolution::OneMin, day));
  auto empty = store.get_session("RELIANCE", algocraft::BarResolution::OneMin, day, 1);
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());

  std::vector<algocraft::BarEvent> bars{sample_bar(1, 50, 10000)};
  const auto day2 = algocraft::SessionDate::from_iso("2026-09-10");
  store.put_session("RELIANCE", algocraft::BarResolution::OneMin, day2, bars);

  auto got = store.get_session("RELIANCE", algocraft::BarResolution::OneMin, day2, 1);
  ASSERT_TRUE(got);
  ASSERT_EQ(got->size(), 1u);
  EXPECT_EQ(got->front().close.paise(), 10005);

  const auto listed = store.list_sessions("RELIANCE", algocraft::BarResolution::OneMin, {}, {});
  ASSERT_EQ(listed.size(), 2u);
  EXPECT_EQ(listed[0].iso(), "2026-09-10");
  EXPECT_EQ(listed[1].iso(), "2026-09-11");
  EXPECT_EQ(algocraft::make_session_key("RELIANCE", algocraft::BarResolution::OneMin, day),
            "RELIANCE|1m|2026-09-11");

  store.close();
  EXPECT_FALSE(store.is_open());
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(BarResolution, ChartCodesAndMonth) {
  EXPECT_EQ(algocraft::bar_resolution_code(algocraft::BarResolution::OneDay), "1d");
  EXPECT_EQ(algocraft::bar_resolution_code(algocraft::BarResolution::OneWeek), "1w");
  EXPECT_EQ(algocraft::bar_resolution_code(algocraft::BarResolution::OneMonth), "1M");
  EXPECT_TRUE(algocraft::is_chart_resolution(algocraft::BarResolution::OneDay));
  EXPECT_TRUE(algocraft::is_chart_resolution(algocraft::BarResolution::OneWeek));
  EXPECT_TRUE(algocraft::is_chart_resolution(algocraft::BarResolution::OneMonth));
  EXPECT_FALSE(algocraft::is_chart_resolution(algocraft::BarResolution::OneMin));
  EXPECT_EQ(algocraft::bar_resolution_from_code("1M"), algocraft::BarResolution::OneMonth);
  EXPECT_FALSE(algocraft::bar_resolution_from_code("1y").has_value());
}

TEST(RocksBarStore, ChartDailyYearBlobRoundTrip) {
  const auto dir = make_temp_dir();
  algocraft::RocksBarStore store(dir);
  store.open();

  const auto period = algocraft::SessionDate::from_iso("2026-03-15");  // any day in year
  EXPECT_EQ(algocraft::make_bar_blob_key("INFY", algocraft::BarResolution::OneDay, period),
            "INFY|1d|2026");
  EXPECT_EQ(algocraft::make_bar_blob_key("INFY", algocraft::BarResolution::OneWeek, period),
            "INFY|1w|2026");
  EXPECT_EQ(algocraft::make_bar_blob_key("INFY", algocraft::BarResolution::OneMonth, period),
            "INFY|1M|2026");

  std::vector<algocraft::BarEvent> bars{
      sample_bar(1, 1'000, 100000, algocraft::BarResolution::OneDay),
      sample_bar(1, 2'000, 101000, algocraft::BarResolution::OneDay),
  };
  store.put_session("INFY", algocraft::BarResolution::OneDay, period, bars);

  // Same year key via a different calendar day.
  const auto same_year = algocraft::SessionDate::from_iso("2026-12-01");
  ASSERT_TRUE(store.has_session("INFY", algocraft::BarResolution::OneDay, same_year));
  auto got = store.get_session("INFY", algocraft::BarResolution::OneDay, same_year, 1);
  ASSERT_TRUE(got);
  ASSERT_EQ(got->size(), 2u);
  EXPECT_EQ(got->front().resolution, algocraft::BarResolution::OneDay);
  EXPECT_EQ(got->front().open.paise(), 100000);
  EXPECT_EQ(got->back().close.paise(), 101005);

  // 1m path still uses full session date — no collision with year blob.
  const auto session = algocraft::SessionDate::from_iso("2026-09-11");
  store.put_session("INFY", algocraft::BarResolution::OneMin, session,
                    {sample_bar(1, 50, 20000)});
  EXPECT_EQ(algocraft::make_session_key("INFY", algocraft::BarResolution::OneMin, session),
            "INFY|1m|2026-09-11");
  ASSERT_TRUE(store.has_session("INFY", algocraft::BarResolution::OneMin, session));

  const auto years = store.list_sessions("INFY", algocraft::BarResolution::OneDay, {}, {});
  ASSERT_EQ(years.size(), 1u);
  EXPECT_EQ(years[0].year(), 2026);
  EXPECT_EQ(years[0].month(), 1);
  EXPECT_EQ(years[0].day(), 1);

  store.close();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
