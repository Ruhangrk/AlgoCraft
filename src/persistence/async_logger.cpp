#include "algocraft/persistence/async_logger.hpp"

#include <algorithm>

#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

bool AsyncLogger::try_log(LogLevel level, std::string_view message) {
  LogEvent event{};
  event.timestamp_ns = Timestamp::now().nanos();
  event.level = level;

  const auto n = std::min(message.size(), event.message.size());
  event.length = static_cast<std::uint16_t>(n);
  if (n > 0) {
    std::copy_n(message.data(), n, event.message.data());
  }
  return ring_->try_push(event);
}

}  // namespace algocraft
