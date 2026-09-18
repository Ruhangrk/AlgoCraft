#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/data_provider.hpp"
#include "algocraft/market_data/historical_loader.hpp"

namespace algocraft {

// Loads token from ~/.config/upstox/config.json (upstox_access_token). Never logs the token.
struct UpstoxConfig {
  std::string access_token;
  std::string history_base_url{"https://api.upstox.com/v3"};
  // Min delay between REST calls (~30 req/min → 2s).
  int min_interval_ms{2100};

  [[nodiscard]] static UpstoxConfig from_default_file();
  [[nodiscard]] static UpstoxConfig from_file(std::string_view path);
  [[nodiscard]] bool ok() const { return !access_token.empty(); }
};

class UpstoxHistoricalLoader final : public HistoricalDataLoader {
public:
  UpstoxHistoricalLoader(UpstoxConfig config, const SymbolTable* symbols);

  // Map trading symbol → Upstox instrument_key (NSE_EQ|ISIN).
  void set_instrument_key(std::string_view ticker, std::string instrument_key);

  std::vector<BarEvent> load_bars(SymbolId symbol_id, Timestamp from, Timestamp to,
                                  BarResolution resolution) override;

  [[nodiscard]] std::uint64_t http_calls() const { return http_calls_; }

private:
  [[nodiscard]] std::string instrument_key_for(SymbolId id) const;
  [[nodiscard]] std::string http_get(std::string_view url) const;

  UpstoxConfig config_{};
  const SymbolTable* symbols_{nullptr};
  std::unordered_map<std::string, std::string> ticker_to_key_{};
  mutable std::uint64_t http_calls_{0};
};

class UpstoxProvider final : public DataProvider {
public:
  explicit UpstoxProvider(UpstoxConfig config, const SymbolTable* symbols = nullptr);

  std::string_view name() const override { return "upstox"; }
  HistoricalDataLoader& historical_loader() override { return loader_; }
  MarketDataFeed* live_feed() override { return nullptr; }  // Phase 9
  DataProviderCapabilities capabilities() const override;

  UpstoxHistoricalLoader& upstox_loader() { return loader_; }

  // NSE EQ keys for the DefaultRouter universe (trading_symbol → ISIN).
  static void register_default_nse_eq(UpstoxHistoricalLoader& loader);

private:
  UpstoxHistoricalLoader loader_;
};

}  // namespace algocraft
