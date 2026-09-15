#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/events.hpp"

namespace algocraft {

struct BacktestResult {
  std::string strategy_name;
  std::int64_t starting_capital_paise{0};
  std::int64_t ending_equity_paise{0};
  std::int64_t realized_pnl_paise{0};
  std::int64_t max_drawdown_paise{0};
  int fills{0};
  int winning_round_trips{0};
  int round_trips{0};
  double win_rate{0};
  double sharpe{0};
};

}  // namespace algocraft
