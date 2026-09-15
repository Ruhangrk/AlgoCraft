#pragma once

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

struct PortfolioView {
  Quantity position{};
  Capital cash{};
};

}  // namespace algocraft
