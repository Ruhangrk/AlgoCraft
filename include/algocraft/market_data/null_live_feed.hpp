#pragma once

#include "algocraft/market_data/market_data_feed.hpp"

namespace algocraft {

// 5.9 placeholder when a vendor has no live tape.
class NullLiveFeed final : public MarketDataFeed {
public:
  void connect() override {}
  void subscribe(SymbolId) override {}
  void unsubscribe(SymbolId) override {}
};

}  // namespace algocraft
