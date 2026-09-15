#include "algocraft/execution/cost_calculator.hpp"

namespace algocraft {
namespace {

std::int64_t millionths(std::int64_t notional_paise, std::int64_t millionths_rate) {
  if (notional_paise <= 0 || millionths_rate <= 0) {
    return 0;
  }
  return (notional_paise * millionths_rate + 500'000) / 1'000'000;
}

}  // namespace

Capital CostCalculator::buy_fees(Price price, Quantity qty) const {
  const auto n = notional(price, qty).paise();
  const auto stamp = millionths(n, rates_.stamp_buy_millionths);
  const auto sebi = millionths(n, rates_.sebi_millionths);
  const auto gst = (rates_.brokerage.paise() * rates_.gst_brokerage_percent + 50) / 100;
  return Capital::from_paise(stamp + sebi + rates_.brokerage.paise() + gst);
}

Capital CostCalculator::sell_fees(Price price, Quantity qty) const {
  const auto n = notional(price, qty).paise();
  const auto stt = millionths(n, rates_.stt_sell_millionths);
  const auto sebi = millionths(n, rates_.sebi_millionths);
  const auto gst = (rates_.brokerage.paise() * rates_.gst_brokerage_percent + 50) / 100;
  return Capital::from_paise(stt + sebi + rates_.brokerage.paise() + gst);
}

}  // namespace algocraft
