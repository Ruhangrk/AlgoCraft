#include "algocraft/market_data/dummy_provider.hpp"

#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {

std::vector<BarEvent> DummyHistoricalLoader::load_bars(SymbolId symbol_id, Timestamp from,
                                                       Timestamp to, BarResolution resolution) {
  (void)from;
  (void)to;
  BarEvent first{};
  first.symbol_id = symbol_id;
  first.timestamp = from;
  first.resolution = resolution;
  first.open = Price::from_paise(10000);
  first.high = Price::from_paise(10100);
  first.low = Price::from_paise(9900);
  first.close = Price::from_paise(10050);
  first.volume = Quantity::from_shares(1000);

  BarEvent second = first;
  second.timestamp = to;
  second.close = Price::from_paise(10080);
  return {first, second};
}

DataProviderCapabilities DummyProvider::capabilities() const {
  return DataProviderCapabilities{
      .supported_resolutions = {BarResolution::OneMin},
      .max_historical_lookback_days = 0,
      .rate_limit_per_second = 0,
  };
}

}  // namespace algocraft
