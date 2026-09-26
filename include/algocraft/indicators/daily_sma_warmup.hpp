#pragma once

#include <vector>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/session_calendar.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

// ~200 session days ≈ 280 calendar; pad to ~320 for holidays.
inline constexpr int kDefaultSmaPeriod = 200;
inline constexpr int kSmaCalendarLookbackDays = 320;

inline bool strategy_needs_daily_sma(const StrategyMetadata& meta) {
  for (const auto& name : meta.required_indicators) {
    if (name == "SMA") {
      return true;
    }
  }
  return false;
}

inline Timestamp daily_sma_warmup_from(Timestamp as_of) {
  auto d = SessionDate::from_ist(as_of);
  for (int i = 0; i < kSmaCalendarLookbackDays; ++i) {
    d = prev_calendar_day(d);
  }
  return session_day_start(d);
}

inline Timestamp daily_sma_warmup_to(Timestamp as_of) {
  return session_day_end(last_closed_session(SessionDate::from_ist(as_of)));
}

// Uses HistoricalDataLoader (CachedProvider → DataFetchService) so RocksDB is
// ensured/filled for OneDay before returning bars.
inline std::vector<BarEvent> load_daily_sma_warmup(HistoricalDataLoader& loader, SymbolId symbol_id,
                                                   Timestamp as_of) {
  const auto from = daily_sma_warmup_from(as_of);
  const auto to = daily_sma_warmup_to(as_of);
  if (from.nanos() == 0 || to.nanos() == 0 || from > to) {
    return {};
  }
  return loader.load_bars(symbol_id, from, to, BarResolution::OneDay);
}

}  // namespace algocraft
