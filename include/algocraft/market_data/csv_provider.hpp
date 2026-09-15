#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/data_provider.hpp"
#include "algocraft/market_data/historical_loader.hpp"

namespace algocraft {

class CsvHistoricalLoader final : public HistoricalDataLoader {
public:
  explicit CsvHistoricalLoader(std::filesystem::path data_dir, const SymbolTable* symbols = nullptr);

  void set_file(SymbolId symbol_id, std::filesystem::path path);

  std::vector<BarEvent> load_bars(SymbolId symbol_id, Timestamp from, Timestamp to,
                                  BarResolution resolution) override;

private:
  std::filesystem::path data_dir_{};
  const SymbolTable* symbols_{nullptr};
  std::unordered_map<SymbolId, std::filesystem::path> files_{};
};

class CsvProvider final : public DataProvider {
public:
  explicit CsvProvider(std::filesystem::path data_dir, const SymbolTable* symbols = nullptr);

  std::string_view name() const override { return "csv"; }
  HistoricalDataLoader& historical_loader() override { return loader_; }
  MarketDataFeed* live_feed() override { return nullptr; }
  DataProviderCapabilities capabilities() const override;

  CsvHistoricalLoader& csv_loader() { return loader_; }

private:
  CsvHistoricalLoader loader_;
};

}  // namespace algocraft
