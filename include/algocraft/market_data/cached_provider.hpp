#pragma once

#include <memory>
#include <string_view>

#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/data_fetch_service.hpp"
#include "algocraft/market_data/data_provider.hpp"
#include "algocraft/persistence/coverage_repository.hpp"

namespace algocraft {

// Wraps a vendor DataProvider. historical_loader() is DataFetchService (RocksDB after ingest).
class CachedProvider final : public DataProvider {
public:
  CachedProvider(std::unique_ptr<DataProvider> inner, BarStore& store, CoverageRepository& coverage,
                 const SymbolTable& symbols);

  std::string_view name() const override { return inner_->name(); }
  HistoricalDataLoader& historical_loader() override { return fetch_; }
  MarketDataFeed* live_feed() override { return inner_->live_feed(); }
  DataProviderCapabilities capabilities() const override { return inner_->capabilities(); }

  DataFetchService& fetch() { return fetch_; }
  DataProvider& vendor() { return *inner_; }

private:
  std::unique_ptr<DataProvider> inner_;
  DataFetchService fetch_;
};

}  // namespace algocraft
