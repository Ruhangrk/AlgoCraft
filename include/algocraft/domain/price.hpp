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

}  // namespace algocraft
