#include "algocraft/execution/simulated_exchange.hpp"

#include <algorithm>
#include <cstdint>

#include "algocraft/domain/capital.hpp"

namespace algocraft {

SimulatedExchange::SimulatedExchange(CostCalculator costs, Price slippage, int mis_leverage)
    : costs_{std::move(costs)}, slippage_{slippage}, mis_leverage_{mis_leverage} {}

std::optional<FillEvent> SimulatedExchange::submit(const OrderIntent& intent, const BarEvent& bar,
                                                   TradingMode mode, Capital cash,
                                                   Quantity position) {
  if (intent.quantity.shares() <= 0) {
    return std::nullopt;
  }

  Price fill_price = bar.close;
  if (intent.side == Side::Buy) {
    fill_price = Price::from_paise(bar.close.paise() + slippage_.paise());
  } else {
    fill_price = Price::from_paise(std::max<std::int64_t>(0, bar.close.paise() - slippage_.paise()));
  }

  if (intent.side == Side::Buy && position.shares() != 0) {
    return std::nullopt;
  }
  if (intent.side == Side::Sell && position.shares() <= 0) {
    return std::nullopt;
  }
  if (intent.side == Side::Sell && intent.quantity.shares() > position.shares()) {
    return std::nullopt;
  }

  const auto value = notional(fill_price, intent.quantity);
  const auto leverage = (mode == TradingMode::Mis) ? mis_leverage_ : 1;
  const auto required = Capital::from_paise(value.paise() / leverage);
  const auto fees =
      (intent.side == Side::Buy) ? costs_.buy_fees(fill_price, intent.quantity)
                                 : costs_.sell_fees(fill_price, intent.quantity);

  if (intent.side == Side::Buy && cash.paise() < required.paise() + fees.paise()) {
    return std::nullopt;
  }

  FillEvent fill{};
  fill.order_id = OrderId::from(next_order_id_++);
  fill.symbol_id = intent.symbol_id;
  fill.side = intent.side;
  fill.filled_qty = intent.quantity;
  fill.fill_price = fill_price;
  fill.fees = fees;
  fill.timestamp = bar.timestamp;
  return fill;
}

}  // namespace algocraft
