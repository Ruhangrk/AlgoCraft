#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "algocraft/domain/symbol.hpp"
#include "algocraft/market_data/bar_store.hpp"
#include "algocraft/market_data/historical_loader.hpp"
#include "algocraft/persistence/coverage_repository.hpp"

namespace algocraft {

// Fills RocksDB from the vendor loader on miss, then serves only from BarStore.
class DataFetchService final : public HistoricalDataLoader {
public:
  static constexpr std::int64_t kLiveTtlNs = 30LL * 60LL * 1'000'000'000LL;
  static constexpr int kMaxChunkCalendarDays = 28;

  DataFetchService(BarStore& store, CoverageRepository& coverage, HistoricalDataLoader& vendor,
                   const SymbolTable& symbols, std::string source = "csv");

  void set_now(Timestamp ts) { now_override_ = ts; }

  void ensure_data_available(std::string_view ticker, Timestamp from, Timestamp to,
                             BarResolution resolution);

  std::vector<BarEvent> load_bars(SymbolId symbol_id, Timestamp from, Timestamp to,
                                  BarResolution resolution) override;

  [[nodiscard]] std::uint64_t vendor_fetches() const { return vendor_fetches_; }

private:
  [[nodiscard]] Timestamp now() const;
  SymbolId require_symbol(std::string_view ticker) const;
  void ensure_chart_available(std::string_view ticker, Timestamp from, Timestamp to,
                              BarResolution resolution);
  void seal_stale_live(std::string_view ticker, BarResolution resolution, SessionDate today,
                       bool today_is_session);
  void fill_closed(std::string_view ticker, SessionDate from, SessionDate to,
                   BarResolution resolution);
  void discover_closed(std::string_view ticker, SessionDate closed_to, BarResolution resolution);
  void refresh_today(std::string_view ticker, SessionDate today, BarResolution resolution);
  void fetch_and_store(std::string_view ticker, const std::vector<SessionDate>& days,
                       BarResolution resolution, bool left_extend);
  void persist_coverage(std::string_view ticker, BarResolution resolution, CoverageRow row);
  [[nodiscard]] bool live_fresh(const CoverageRow& row) const;

  BarStore* store_{nullptr};
  CoverageRepository* coverage_{nullptr};
  HistoricalDataLoader* vendor_{nullptr};
  const SymbolTable* symbols_{nullptr};
  std::string source_{"csv"};
  std::uint64_t vendor_fetches_{0};
  std::optional<Timestamp> now_override_{};
};

}  // namespace algocraft
