#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "algocraft/domain/bar_resolution.hpp"

namespace algocraft {

struct DataProviderCapabilities {
  std::vector<BarResolution> supported_resolutions{};
  std::int32_t max_historical_lookback_days{0};
  std::int32_t rate_limit_per_second{0};
};

class HistoricalDataLoader;
class MarketDataFeed;

class DataProvider {
public:
  virtual ~DataProvider() = default;

  virtual std::string_view name() const = 0;
  virtual HistoricalDataLoader& historical_loader() = 0;
  virtual MarketDataFeed* live_feed() = 0;
  virtual DataProviderCapabilities capabilities() const = 0;
};

}  // namespace algocraft
