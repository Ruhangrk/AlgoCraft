#pragma once

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

struct BarEvent {
  SymbolId symbol_id{0};
  Timestamp timestamp{};
  BarResolution resolution{BarResolution::OneMin};
  Price open{};
  Price high{};
  Price low{};
  Price close{};
  Quantity volume{};
};

}  // namespace algocraft
