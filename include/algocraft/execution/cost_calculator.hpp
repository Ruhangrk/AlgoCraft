#pragma once

#include <cstdint>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

struct CostRates {
  // Fractions of notional, expressed as integer millionths (1e-6).
  // 0.025% STT = 250 millionths; 0.003% stamp = 30; 0.0001% SEBI = 1.
  std::int64_t stt_sell_millionths{250};
  std::int64_t stamp_buy_millionths{30};
  std::int64_t sebi_millionths{1};
  Capital brokerage{Capital::from_paise(2000)};  // ₹20
  std::int64_t gst_brokerage_percent{18};
};

class CostCalculator {
public:
  CostCalculator() = default;
  explicit CostCalculator(CostRates rates) : rates_{rates} {}

  [[nodiscard]] Capital buy_fees(Price price, Quantity qty) const;
  [[nodiscard]] Capital sell_fees(Price price, Quantity qty) const;

private:
  CostRates rates_{};
};

}  // namespace algocraft
