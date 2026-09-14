#pragma once

#include <vector>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/symbol.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft {

class HistoricalDataLoader {
public:
  virtual ~HistoricalDataLoader() = default;

  virtual std::vector<BarEvent> load_bars(SymbolId symbol_id, Timestamp from, Timestamp to,
                                          BarResolution resolution) = 0;
};

}  // namespace algocraft
