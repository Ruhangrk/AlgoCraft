#pragma once

#include <compare>
#include <cstdint>

#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

// Cash in paise, same scale as Price, separate type so we do not mix ticks with capital.
class Capital {
public:
  constexpr Capital() = default;

  static constexpr Capital from_paise(std::int64_t paise) { return Capital{paise}; }

  constexpr std::int64_t paise() const { return paise_; }

  constexpr Capital operator+(Capital other) const { return Capital{paise_ + other.paise_}; }
  constexpr Capital operator-(Capital other) const { return Capital{paise_ - other.paise_}; }

  constexpr auto operator<=>(const Capital&) const = default;

private:
  explicit constexpr Capital(std::int64_t paise) : paise_{paise} {}

  std::int64_t paise_{0};
};

constexpr Capital notional(Price price, Quantity quantity) {
  return Capital::from_paise(price.paise() * quantity.shares());
}

}  // namespace algocraft
