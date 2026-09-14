#pragma once

#include "algocraft/domain/symbol.hpp"

namespace algocraft {

class MarketDataFeed {
public:
  virtual ~MarketDataFeed() = default;

  virtual void connect() = 0;
  virtual void subscribe(SymbolId symbol_id) = 0;
  virtual void unsubscribe(SymbolId symbol_id) = 0;
};

}  // namespace algocraft
