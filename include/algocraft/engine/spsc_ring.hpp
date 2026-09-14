#pragma once

#include <cstddef>
#include <type_traits>

#include <boost/lockfree/spsc_queue.hpp>

namespace algocraft {

/// SPSC ring facade. Swap the Boost implementation later without changing callers.
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

  [[nodiscard]] bool try_push(const T& item) { return queue_.push(item); }

  [[nodiscard]] bool try_pop(T& item) { return queue_.pop(item); }

private:
  boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Capacity>> queue_{};
};

}  // namespace algocraft
