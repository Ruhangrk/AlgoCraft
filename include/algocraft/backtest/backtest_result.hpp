#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/events.hpp"

namespace algocraft {

struct LoggedFill {
  std::int64_t timestamp_ns{0};
  Side side{Side::Buy};
  std::int64_t price_paise{0};
  std::int64_t qty{0};
  std::int64_t fees_paise{0};
};

struct DailySnapshot {
  std::int64_t timestamp_ns{0};
  std::int64_t realized_pnl_paise{0};
  std::int64_t fees_paise{0};
  std::int64_t eod_equity_paise{0};
  int fills{0};
  int round_trips{0};
  int wins{0};
};

struct LoggedSignal {
  std::int64_t timestamp_ns{0};
  int intent_count{0};
  std::string indicators_json;
};

struct LoggedRejection {
  std::int64_t timestamp_ns{0};
  std::string rule;
  std::string reason;
};

struct BacktestResult {
  std::string strategy_name;
  std::int64_t starting_capital_paise{0};
  std::int64_t ending_equity_paise{0};
  std::int64_t realized_pnl_paise{0};
  std::int64_t fees_paise{0};
  std::int64_t max_drawdown_paise{0};
  int fills{0};
  int winning_round_trips{0};
  int round_trips{0};
  double win_rate{0};
  double sharpe{0};
  double avg_hold_seconds{0};
  std::size_t bars{0};
  std::vector<DailySnapshot> daily{};
  std::vector<LoggedFill> fills_log{};
  // In-memory only for now (manual backtest does not persist these yet).
  std::vector<LoggedSignal> signals{};
  std::vector<LoggedRejection> rejections{};
};

}  // namespace algocraft
