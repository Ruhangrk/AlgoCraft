#pragma once

#include <compare>
#include <cstdint>

namespace algocraft {

// NSE cash prices in paise (₹1 = 100). Integer so money never uses float.
class Price {
public:
  constexpr Price() = default;

  static constexpr Price from_paise(std::int64_t paise) { return Price{paise}; }

  constexpr std::int64_t paise() const { return paise_; }

  constexpr Price operator+(Price other) const { return Price{paise_ + other.paise_}; }
  constexpr Price operator-(Price other) const { return Price{paise_ - other.paise_}; }

  constexpr auto operator<=>(const Price&) const = default;

private:
  explicit constexpr Price(std::int64_t paise) : paise_{paise} {}

  std::int64_t paise_{0};
};

// Round to nearest tick (tick is also in paise, e.g. ₹0.05 = 5).
constexpr Price round_to_tick(Price price, Price tick) {
  const auto step = tick.paise();
  if (step <= 0) {
    return price;
  }
  const auto raw = price.paise();
  const auto rounded = ((raw + step / 2) / step) * step;
  return Price::from_paise(rounded);
}

}  // namespace algocraft
