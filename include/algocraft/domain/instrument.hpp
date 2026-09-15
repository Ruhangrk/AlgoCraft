#pragma once

#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

struct Instrument {
  SymbolId symbol_id{0};
  InstrumentType type{InstrumentType::Equity};
  Quantity lot_size{Quantity::from_shares(1)};
  Price tick_size{Price::from_paise(1)};
  Currency currency{Currency::Inr};
  Timestamp expiry{};  // zero = no expiry (cash equity)
};

}  // namespace algocraft
