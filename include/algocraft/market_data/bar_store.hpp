#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/session_date.hpp"

namespace algocraft {

// Session OHLCV warehouse. Engine talks to this, not to RocksDB types.
class BarStore {
public:
  virtual ~BarStore() = default;

  virtual void put_session(std::string_view ticker, BarResolution resolution, SessionDate date,
                           const std::vector<BarEvent>& bars) = 0;

  [[nodiscard]] virtual std::optional<std::vector<BarEvent>> get_session(
      std::string_view ticker, BarResolution resolution, SessionDate date,
      SymbolId symbol_id) const = 0;

  [[nodiscard]] virtual bool has_session(std::string_view ticker, BarResolution resolution,
                                         SessionDate date) const = 0;

  [[nodiscard]] virtual std::vector<SessionDate> list_sessions(std::string_view ticker,
                                                               BarResolution resolution,
                                                               SessionDate from,
                                                               SessionDate to) const = 0;
};

}  // namespace algocraft
