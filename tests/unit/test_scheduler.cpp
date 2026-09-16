#include "algocraft/domain/session_clock.hpp"
#include "algocraft/scheduler/session_scheduler.hpp"

#include <chrono>
#include <gtest/gtest.h>

using algocraft::SessionScheduler;
using algocraft::SystemEventType;

namespace {

algocraft::Timestamp ist_clock(int hour, int minute) {
  using namespace std::chrono;
  const auto utc =
      sys_days{year{2026} / 8 / 28} + hours{hour} + minutes{minute} - hours{5} - minutes{30};
  return algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

}  // namespace

TEST(SessionScheduler, StartMisEnd) {
  SessionScheduler sched;
  const auto open = sched.on_bar(ist_clock(9, 15));
  ASSERT_EQ(open.size(), 1u);
  EXPECT_EQ(open[0].type, SystemEventType::SessionStart);

  const auto mid = sched.on_bar(ist_clock(10, 0));
  EXPECT_TRUE(mid.empty());

  const auto mis = sched.on_bar(ist_clock(15, 15));
  ASSERT_EQ(mis.size(), 1u);
  EXPECT_EQ(mis[0].type, SystemEventType::MisSquareoffWarning);

  const auto again = sched.on_bar(ist_clock(15, 20));
  EXPECT_TRUE(again.empty());

  const auto end = sched.flush(ist_clock(15, 29));
  ASSERT_TRUE(end.has_value());
  EXPECT_EQ(end->type, SystemEventType::SessionEnd);
}

TEST(SessionScheduler, NewDayRestarts) {
  SessionScheduler sched;
  sched.on_bar(ist_clock(10, 0));
  using namespace std::chrono;
  const auto next_day_utc =
      sys_days{year{2026} / 8 / 29} + hours{10} - hours{5} - minutes{30};
  const auto next = algocraft::Timestamp::from_nanos(
      duration_cast<nanoseconds>(next_day_utc.time_since_epoch()).count());
  const auto evs = sched.on_bar(next);
  ASSERT_EQ(evs.size(), 2u);
  EXPECT_EQ(evs[0].type, SystemEventType::SessionEnd);
  EXPECT_EQ(evs[1].type, SystemEventType::SessionStart);
}

TEST(SessionClock, PositionWindowMinutes) {
  EXPECT_EQ(algocraft::ist_minute_of_day(ist_clock(15, 15)), 15 * 60 + 15);
}
