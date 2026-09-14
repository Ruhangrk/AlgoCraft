#pragma once

#include <chrono>
#include <compare>
#include <cstdint>

namespace algocraft {

class Timestamp {
public:
  constexpr Timestamp() = default;

  static constexpr Timestamp from_nanos(std::int64_t ns) { return Timestamp{ns}; }

  static Timestamp now() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    return Timestamp{ns};
  }

  constexpr std::int64_t nanos() const { return nanos_; }

  constexpr auto operator<=>(const Timestamp&) const = default;

private:
  explicit constexpr Timestamp(std::int64_t nanos) : nanos_{nanos} {}

  std::int64_t nanos_{0};
};

}  // namespace algocraft
