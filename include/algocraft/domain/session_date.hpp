#pragma once

#include <compare>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

// IST trading-session calendar date (YYYY-MM-DD), not a civil UTC date.
struct SessionDate {
  std::int32_t ymd{0};

  static SessionDate from_parts(int year, int month, int day) {
    return SessionDate{year * 10000 + month * 100 + day};
  }

  static SessionDate from_iso(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
      throw std::invalid_argument("bad session date: " + std::string{text});
    }
    const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 +
                     (text[3] - '0');
    const int month = (text[5] - '0') * 10 + (text[6] - '0');
    const int day = (text[8] - '0') * 10 + (text[9] - '0');
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
      throw std::invalid_argument("bad session date: " + std::string{text});
    }
    return from_parts(year, month, day);
  }

  static SessionDate from_ist(Timestamp ts) {
    const auto ist_sec = static_cast<std::time_t>(ts.nanos() / 1'000'000'000LL + 19800);
    std::tm out{};
    gmtime_r(&ist_sec, &out);
    return from_parts(out.tm_year + 1900, out.tm_mon + 1, out.tm_mday);
  }

  [[nodiscard]] int year() const { return ymd / 10000; }
  [[nodiscard]] int month() const { return (ymd / 100) % 100; }
  [[nodiscard]] int day() const { return ymd % 100; }
  [[nodiscard]] bool ok() const { return ymd > 0; }

  [[nodiscard]] std::string iso() const {
    // 16: GCC -Wformat-truncation treats year() as unbounded int; 11 is exact for YYYY-MM-DD.
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year(), month(), day());
    return buf;
  }

  constexpr auto operator<=>(const SessionDate&) const = default;
};

inline std::string make_session_key(std::string_view ticker, BarResolution resolution,
                                    SessionDate date) {
  std::string key;
  key.reserve(ticker.size() + 16);
  key.append(ticker);
  key.push_back('|');
  key.append(bar_resolution_code(resolution));
  key.push_back('|');
  key.append(date.iso());
  return key;
}

}  // namespace algocraft
