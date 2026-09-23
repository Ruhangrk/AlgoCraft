#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "algocraft/container/container_manager.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/session_calendar.hpp"
#include "algocraft/domain/session_date.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/market_data/data_source_registry.hpp"
#include "algocraft/routing/routing_algo.hpp"
#include "algocraft/strategies/strategy_registry.hpp"
#include "algocraft/workbook/workbook_manager.hpp"

namespace algocraft { class ActivityRepository; }

namespace algocraft {

struct RunConfig {
  UserId user_id{};
  std::string workbook_name{"phase4"};
  Capital workbook_capital{Capital::from_paise(10'00'00'000'00)};
  // When set, run under this workbook (must already be adopt()'d or create()'d in books).
  std::optional<WorkbookId> existing_workbook_id{};
  std::vector<std::string> tickers{};
  std::vector<std::string> strategies{"ema_crossover", "vwap_reversion", "consecutive_up_clip"};
  // Eval window (router backtests) and trade tape window (hist replay for Part 1).
  Timestamp from{};
  Timestamp to{};
  Timestamp trade_from{};
  Timestamp trade_to{};
  // When set via API anchor_date resolution (informational / response echo).
  std::optional<SessionDate> anchor_date{};
  int eval_sessions{0};
  std::string router{"default_router"};
  std::size_t max_replay_bars{0};
};

struct ResolvedRunWindows {
  SessionDate anchor{};
  SessionDate eval_first{};
  SessionDate eval_last{};
  Timestamp eval_from{};
  Timestamp eval_to{};
  Timestamp trade_from{};
  Timestamp trade_to{};
  int eval_sessions{0};
};

// Anchor = trade day. Eval = previous `eval_sessions` NSE sessions (excluding anchor).
// Trade tape = hist bars on the anchor day (full day, or IST minute range).
inline ResolvedRunWindows resolve_anchor_run_windows(SessionDate anchor, int eval_sessions,
                                                    std::optional<int> trade_from_minute = {},
                                                    std::optional<int> trade_to_minute = {}) {
  if (!anchor.ok()) {
    throw std::invalid_argument("anchor_date required");
  }
  if (!is_nse_session_day(anchor)) {
    throw std::invalid_argument("anchor_date must be an NSE session day");
  }
  if (eval_sessions < 1) {
    throw std::invalid_argument("eval_sessions must be >= 1");
  }

  ResolvedRunWindows out{};
  out.anchor = anchor;
  out.eval_sessions = eval_sessions;
  out.eval_last = prev_session_day(anchor);
  out.eval_first = out.eval_last;
  for (int i = 1; i < eval_sessions; ++i) {
    out.eval_first = prev_session_day(out.eval_first);
  }
  out.eval_from = session_day_start(out.eval_first);
  out.eval_to = session_day_end(out.eval_last);

  if (trade_from_minute.has_value() != trade_to_minute.has_value()) {
    throw std::invalid_argument("trade_from and trade_to must both be set or both omitted");
  }
  if (trade_from_minute) {
    if (*trade_from_minute >= *trade_to_minute) {
      throw std::invalid_argument("trade_from must be before trade_to");
    }
    out.trade_from = ist_at(anchor, *trade_from_minute / 60, *trade_from_minute % 60);
    out.trade_to = ist_at(anchor, *trade_to_minute / 60, *trade_to_minute % 60);
  } else {
    out.trade_from = session_day_start(anchor);
    out.trade_to = session_day_end(anchor);
  }
  return out;
}

inline void apply_run_windows(RunConfig& cfg, const ResolvedRunWindows& w) {
  cfg.from = w.eval_from;
  cfg.to = w.eval_to;
  cfg.trade_from = w.trade_from;
  cfg.trade_to = w.trade_to;
  cfg.anchor_date = w.anchor;
  cfg.eval_sessions = w.eval_sessions;
}

struct RunResult {
  WorkbookId workbook_id{};
  BorrowId borrow_id{};
  Capital workbook_available_before{};
  Capital workbook_available_after{};
  Capital returned{};
  std::vector<StrategyEvalResult> evaluations{};
  std::vector<ContainerManager::Snapshot> traded{};
  std::vector<ContainerManager::SignalLog> signals{};
  std::vector<ContainerManager::RejectionLog> rejections{};
  int selected{0};
  int skipped{0};
  int real_containers{0};
  int fills{0};
  bool force_stopped{false};
};

class RunManager {
public:
  // repo is optional; when non-null, persists workbook/run/containers/fills after the run.
  RunResult execute(const RunConfig& config, DataSourceRegistry& data, StrategyRegistry& strategies,
                    WorkbookManager& books, SymbolTable& symbols,
                    ActivityRepository* repo = nullptr);
};

}  // namespace algocraft
