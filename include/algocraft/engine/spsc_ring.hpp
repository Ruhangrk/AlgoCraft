#pragma once

#include <cstddef>
#include <type_traits>

#include <boost/lockfree/spsc_queue.hpp>

namespace algocraft {

// One writer, one reader. Fixed size so the queue never grows at runtime.
// Boost is behind this type so we can replace it later without touching callers.
template <typename T, std::size_t Capacity>
class SpscRing {
  static_assert(Capacity > 0, "SpscRing capacity must be > 0");
  static_assert(std::is_trivially_copyable_v<T>, "SpscRing T must be trivially copyable");

public:
  using value_type = T;

  static constexpr std::size_t capacity() { return Capacity; }

  SpscRing() = default;

  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;

  // false = ring full; caller must not wait on the hot path.
  [[nodiscard]] bool try_push(const T& item) { return queue_.push(item); }

  // false = ring empty.
  [[nodiscard]] bool try_pop(T& item) { return queue_.pop(item); }

private:
  boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Capacity>> queue_{};
};

}  // namespace algocraft
