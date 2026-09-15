#include "algocraft/domain/symbol.hpp"

#include <stdexcept>
#include <utility>

namespace algocraft {

SymbolId SymbolTable::intern(Symbol symbol, Instrument instrument) {
  if (symbol.ticker.empty()) {
    throw std::invalid_argument("ticker is empty");
  }

  if (const auto it = ticker_to_id_.find(symbol.ticker);
      it != ticker_to_id_.end()) {
    return it->second;
  }

  const auto id = static_cast<SymbolId>(symbols_.size() + 1);
  instrument.symbol_id = id;
  symbols_.push_back(std::move(symbol));
  instruments_.push_back(instrument);
  ticker_to_id_.emplace(symbols_.back().ticker, id);
  return id;
}

bool SymbolTable::contains(SymbolId id) const {
  return id != 0 && id <= symbols_.size();
}

const Symbol &SymbolTable::symbol(SymbolId id) const {
  if (!contains(id)) {
    throw std::out_of_range("unknown symbol id");
  }
  return symbols_[id - 1];
}

const Instrument &SymbolTable::instrument(SymbolId id) const {
  if (!contains(id)) {
    throw std::out_of_range("unknown symbol id");
  }
  return instruments_[id - 1];
}

} // namespace algocraft
