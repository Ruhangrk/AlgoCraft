#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

#include "algocraft/domain/session_date.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

// Sakamoto: 0 = Sunday.
inline int weekday_sun0(SessionDate date) {
  static constexpr int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = date.year();
  const int m = date.month();
  const int d = date.day();
  if (m < 3) {
    --y;
  }
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

inline bool is_weekend(SessionDate date) {
  const auto w = weekday_sun0(date);
  return w == 0 || w == 6;
}

// Static NSE holidays (refine later). 5.2 serve-path uses stored keys, not this list.
inline bool is_nse_holiday(SessionDate date) {
  switch (date.ymd) {
    case 20260126:
    case 20260303:
    case 20260403:
    case 20260414:
    case 20260501:
    case 20260815:
    case 20261002:
    case 20261124:
    case 20261225:
      return true;
    default:
      return false;
  }
}

inline bool is_nse_session_day(SessionDate date) {
  return !is_weekend(date) && !is_nse_holiday(date);
}

inline SessionDate next_calendar_day(SessionDate date) {
  const auto leap = [](int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };
  static constexpr int kDim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int y = date.year();
  int m = date.month();
  int d = date.day() + 1;
  const int max_d = (m == 2 && leap(y)) ? 29 : kDim[m];
  if (d > max_d) {
    d = 1;
    ++m;
  }
  if (m > 12) {
    m = 1;
    ++y;
  }
  return SessionDate::from_parts(y, m, d);
}

inline SessionDate next_session_day(SessionDate date) {
  auto d = next_calendar_day(date);
  while (!is_nse_session_day(d)) {
    d = next_calendar_day(d);
  }
  return d;
}

inline SessionDate prev_calendar_day(SessionDate date) {
  const auto leap = [](int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };
  static constexpr int kDim[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int y = date.year();
  int m = date.month();
  int d = date.day() - 1;
  if (d < 1) {
    --m;
    if (m < 1) {
      m = 12;
      --y;
    }
    d = (m == 2 && leap(y)) ? 29 : kDim[m];
  }
  return SessionDate::from_parts(y, m, d);
}

inline SessionDate prev_session_day(SessionDate date) {
  auto d = prev_calendar_day(date);
  while (!is_nse_session_day(d)) {
    d = prev_calendar_day(d);
  }
  return d;
}

inline SessionDate last_closed_session(SessionDate ist_today) {
  if (is_nse_session_day(ist_today)) {
    return prev_session_day(ist_today);
  }
  auto d = ist_today;
  while (!is_nse_session_day(d)) {
    d = prev_calendar_day(d);
  }
  return d;
}

inline std::vector<SessionDate> session_days(SessionDate from, SessionDate to) {
  std::vector<SessionDate> out;
  if (!from.ok() || !to.ok() || from > to) {
    return out;
  }
  for (auto d = from; d <= to; d = next_calendar_day(d)) {
    if (is_nse_session_day(d)) {
      out.push_back(d);
    }
  }
  return out;
}

inline Timestamp ist_at(SessionDate date, int hour, int minute) {
  using namespace std::chrono;
  const auto utc = sys_days{year{date.year()} / date.month() / date.day()} + hours{hour} +
                   minutes{minute} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

inline Timestamp session_day_start(SessionDate date) { return ist_at(date, 0, 0); }
inline Timestamp session_day_end(SessionDate date) { return ist_at(date, 23, 59); }

// "HH:MM" → minutes from IST midnight.
inline std::optional<int> parse_hhmm(std::string_view text) {
  if (text.size() != 5 || text[2] != ':') {
    return std::nullopt;
  }
  if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' || text[3] < '0' ||
      text[3] > '9' || text[4] < '0' || text[4] > '9') {
    return std::nullopt;
  }
  const int hour = (text[0] - '0') * 10 + (text[1] - '0');
  const int minute = (text[3] - '0') * 10 + (text[4] - '0');
  if (hour > 23 || minute > 59) {
    return std::nullopt;
  }
  return hour * 60 + minute;
}

}  // namespace algocraft
