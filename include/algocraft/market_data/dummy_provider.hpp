#pragma once

#include "algocraft/market_data/data_provider.hpp"
#include "algocraft/market_data/historical_loader.hpp"

namespace algocraft {

class DummyHistoricalLoader : public HistoricalDataLoader {
public:
  std::vector<BarEvent> load_bars(SymbolId symbol_id, Timestamp from, Timestamp to,
                                  BarResolution resolution) override;
};

class DummyProvider : public DataProvider {
public:
  std::string_view name() const override { return "dummy"; }
  HistoricalDataLoader& historical_loader() override { return loader_; }
  MarketDataFeed* live_feed() override { return nullptr; }
  DataProviderCapabilities capabilities() const override;

private:
  DummyHistoricalLoader loader_{};
};

}  // namespace algocraft
