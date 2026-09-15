#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <type_traits>

namespace algocraft {

// 128-bit id for API/persistence (users, workbooks). Not parsed as a string on Thread 0.
struct Uuid {
  std::array<std::uint8_t, 16> bytes{};

  static Uuid from_u64(std::uint64_t value) {
    Uuid id{};
    for (int i = 7; i >= 0; --i) {
      id.bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(value & 0xff);
      value >>= 8;
    }
    return id;
  }

  constexpr auto operator<=>(const Uuid&) const = default;
};

static_assert(std::is_trivially_copyable_v<Uuid>);
static_assert(sizeof(Uuid) == 16);

template <typename Tag>
struct TypedU64 {
  std::uint64_t value{0};

  static constexpr TypedU64 from(std::uint64_t v) { return TypedU64{v}; }

  constexpr auto operator<=>(const TypedU64&) const = default;
};

struct OrderIdTag {};
struct FillIdTag {};
struct ContainerIdTag {};
struct StrategyIdTag {};
struct RoutingAlgoIdTag {};
struct RunIdTag {};

using OrderId = TypedU64<OrderIdTag>;
using FillId = TypedU64<FillIdTag>;
using ContainerId = TypedU64<ContainerIdTag>;
using StrategyId = TypedU64<StrategyIdTag>;
using RoutingAlgoId = TypedU64<RoutingAlgoIdTag>;
using RunId = TypedU64<RunIdTag>;

using SymbolId = std::uint32_t;
using UserId = Uuid;
using WorkbookId = Uuid;

}  // namespace algocraft
