#pragma once

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

inline Quantity position_for_capital(Capital cap, Price px, int leverage = 5) {
  if (px.paise() <= 0 || cap.paise() <= 0) {
    return Quantity::from_shares(1);
  }
  const auto lev = leverage > 0 ? leverage : 1;
  const auto buying = cap.paise() * lev * 95 / 100;
  auto shares = buying / px.paise();
  if (shares < 1) {
    shares = 1;
  }
  return Quantity::from_shares(shares);
}

}  // namespace algocraft
