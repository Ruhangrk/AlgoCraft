#pragma once

#include <string_view>

#include "algocraft/engine/spsc_ring.hpp"
#include "algocraft/persistence/log_event.hpp"

namespace algocraft {

class AsyncLogger {
public:
  static constexpr std::size_t kCapacity = 4096;

  using Ring = SpscRing<LogEvent, kCapacity>;

  explicit AsyncLogger(Ring& ring) : ring_(&ring) {}

  /// Non-blocking. Returns false if the ring is full (message dropped).
  bool try_log(LogLevel level, std::string_view message);

private:
  Ring* ring_{nullptr};
};

}  // namespace algocraft
