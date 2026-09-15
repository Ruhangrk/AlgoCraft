#pragma once

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/enums.hpp"

namespace algocraft {

struct Account {
  Capital capital{};
  ContainerMode mode{ContainerMode::Backtest};
  AccountStatus status{AccountStatus::Active};
};

}  // namespace algocraft
