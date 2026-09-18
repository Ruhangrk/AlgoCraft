#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "algocraft/domain/bar_event.hpp"

namespace algocraft {

// Packed OHLCV for one RocksDB session blob. Little-endian. Not JSON/CSV.
std::string pack_session_bars(const std::vector<BarEvent>& bars);
std::vector<BarEvent> unpack_session_bars(std::string_view blob, SymbolId symbol_id,
                                          BarResolution resolution);

}  // namespace algocraft
