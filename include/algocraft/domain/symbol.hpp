#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "algocraft/domain/enums.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/instrument.hpp"

namespace algocraft {

// Human-readable identity. Hot path keeps only SymbolId.
struct Symbol {
  Market market{Market::Nse};
  Segment segment{Segment::Eq};
  std::string ticker;
};

class SymbolTable {
public:
  SymbolId intern(Symbol symbol, Instrument instrument);

  [[nodiscard]] bool contains(SymbolId id) const;
  [[nodiscard]] std::optional<SymbolId> find(std::string_view ticker) const;
  [[nodiscard]] const Symbol& symbol(SymbolId id) const;
  [[nodiscard]] const Instrument& instrument(SymbolId id) const;

  [[nodiscard]] std::size_t size() const { return symbols_.size(); }

private:
  std::vector<Symbol> symbols_{};
  std::vector<Instrument> instruments_{};
  std::unordered_map<std::string, SymbolId> ticker_to_id_{};
};

}  // namespace algocraft
