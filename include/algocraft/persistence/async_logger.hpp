#pragma once

#include <string_view>

#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/persistence/log_event.hpp"

namespace algocraft {

// Hot path only pushes. Thread 2 pops and prints. Never blocks; drops if full.
class AsyncLogger {
public:
  static constexpr std::size_t kCapacity = 4096;

  using Ring = SpscRing<LogEvent, kCapacity>;

  explicit AsyncLogger(Ring& ring) : ring_(&ring) {}

  [[nodiscard]] bool try_log(LogLevel level, std::string_view message);

private:
  Ring* ring_{nullptr};
};

}  // namespace algocraft
