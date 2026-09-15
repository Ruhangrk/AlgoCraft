#pragma once

#include <cstdint>
#include <type_traits>

namespace algocraft {

// Stand-in event until Phase 1 adds BarEvent/Order/Fill on the rings.
struct DummyEvent {
  std::uint32_t kind{0};
  std::uint32_t symbol_id{0};
  std::uint64_t seq{0};
};

inline constexpr std::uint32_t kEventBar = 1;
inline constexpr std::uint32_t kEventFill = 2;
inline constexpr std::uint32_t kEventCommand = 3;
inline constexpr std::uint32_t kEventPersist = 4;

static_assert(std::is_trivially_copyable_v<DummyEvent>);

}  // namespace algocraft
