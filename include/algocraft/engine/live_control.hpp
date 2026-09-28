#pragma once

#include <cstdint>
#include <type_traits>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/events.hpp"
#include "algocraft/domain/ids.hpp"

namespace algocraft {

// RoutingRing: T0 → T1 (bar + session edges for routing algo).
enum class RoutingSignalKind : std::uint8_t {
  Bar = 0,
  SessionStart = 1,
  SessionEnd = 2,
};

struct RoutingSignal {
  RoutingSignalKind kind{RoutingSignalKind::Bar};
  BarEvent bar{};
};

static_assert(std::is_trivially_copyable_v<RoutingSignal>);

// CommandRing: control / T1 → T0 (system + container commands).
enum class LiveCommandType : std::uint8_t {
  System = 0,     // SessionStart / SessionEnd / KillSwitch / MisSquareoff
  StopRun = 1,    // request ActiveRun loop exit (after applying system if set)
  KillContainer = 2,
};

struct LiveCommand {
  LiveCommandType type{LiveCommandType::System};
  SystemEvent system{};
  ContainerId container_id{};

  static LiveCommand system_event(SystemEvent ev) {
    LiveCommand c{};
    c.type = LiveCommandType::System;
    c.system = ev;
    return c;
  }

  static LiveCommand kill_switch(Timestamp ts = {}) {
    SystemEvent ev{};
    ev.type = SystemEventType::KillSwitch;
    ev.timestamp = ts.nanos() != 0 ? ts : Timestamp::now();
    return system_event(ev);
  }

  static LiveCommand stop_run() {
    LiveCommand c{};
    c.type = LiveCommandType::StopRun;
    return c;
  }

  static LiveCommand kill_container(ContainerId id) {
    LiveCommand c{};
    c.type = LiveCommandType::KillContainer;
    c.container_id = id;
    return c;
  }
};

static_assert(std::is_trivially_copyable_v<LiveCommand>);
static_assert(std::is_trivially_copyable_v<SystemEvent>);

}  // namespace algocraft
