#pragma once

#include <compare>
#include <cstdint>

namespace algocraft {

class Quantity {
public:
  constexpr Quantity() = default;

  static constexpr Quantity from_shares(std::int64_t shares) { return Quantity{shares}; }

  constexpr std::int64_t shares() const { return shares_; }

  constexpr auto operator<=>(const Quantity&) const = default;

private:
  explicit constexpr Quantity(std::int64_t shares) : shares_{shares} {}

  std::int64_t shares_{0};
};

}  // namespace algocraft
