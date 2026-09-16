#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "algocraft/domain/events.hpp"
#include "algocraft/domain/session_clock.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

class SessionScheduler {
public:
  std::vector<SystemEvent> on_bar(Timestamp ts) {
    std::vector<SystemEvent> out;
    const auto ist = ts.nanos() + kIstOffsetNs;
    const auto day = ist / kNanosPerDay;
    const auto minute = ist_minute_of_day(ts);

    if (last_day_ != day) {
      if (session_open_) {
        SystemEvent end{};
        end.type = SystemEventType::SessionEnd;
        end.timestamp = ts;
        out.push_back(end);
        session_open_ = false;
      }
      last_day_ = day;
      mis_fired_ = false;
      SystemEvent start{};
      start.type = SystemEventType::SessionStart;
      start.timestamp = ts;
      out.push_back(start);
      session_open_ = true;
    }
    if (session_open_ && !mis_fired_ && minute >= 15 * 60 + 15) {
      mis_fired_ = true;
      SystemEvent warn{};
      warn.type = SystemEventType::MisSquareoffWarning;
      warn.timestamp = ts;
      out.push_back(warn);
    }
    return out;
  }

  std::optional<SystemEvent> flush(Timestamp ts) {
    if (!session_open_) {
      return std::nullopt;
    }
    session_open_ = false;
    SystemEvent end{};
    end.type = SystemEventType::SessionEnd;
    end.timestamp = ts;
    return end;
  }

private:
  std::int64_t last_day_{-1};
  bool mis_fired_{false};
  bool session_open_{false};
};

}  // namespace algocraft
