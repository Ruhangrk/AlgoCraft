#pragma once

#include "algocraft/market_data/market_data_feed.hpp"

namespace algocraft {

// 5.9 placeholder: PAPER containers subscribe via DataProvider::live_feed().
// CsvProvider returns nullptr (history-only). A live vendor (Upstox WS) will
// implement MarketDataFeed; do not drive paper on_bar from the REST today blob.
class NullLiveFeed final : public MarketDataFeed {
public:
  void connect() override {}
  void subscribe(SymbolId) override {}
  void unsubscribe(SymbolId) override {}
};

}  // namespace algocraft
