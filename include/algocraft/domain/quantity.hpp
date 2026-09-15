#pragma once

#include <compare>
#include <cstdint>

namespace algocraft {

class Quantity {
public:
  constexpr Quantity() = default;

  static constexpr Quantity from_shares(std::int64_t shares) { return Quantity{shares}; }

  constexpr std::int64_t shares() const { return shares_; }

  constexpr Quantity operator+(Quantity other) const { return Quantity{shares_ + other.shares_}; }
  constexpr Quantity operator-(Quantity other) const { return Quantity{shares_ - other.shares_}; }

  constexpr auto operator<=>(const Quantity&) const = default;

private:
  explicit constexpr Quantity(std::int64_t shares) : shares_{shares} {}

  std::int64_t shares_{0};
};

// Exchange lot: drop leftover shares that are not a full lot.
constexpr Quantity round_down_to_lot(Quantity quantity, Quantity lot_size) {
  const auto lot = lot_size.shares();
  if (lot <= 0) {
    return Quantity::from_shares(0);
  }
  return Quantity::from_shares((quantity.shares() / lot) * lot);
}

}  // namespace algocraft
