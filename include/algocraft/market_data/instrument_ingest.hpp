#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "algocraft/persistence/instrument_repository.hpp"

namespace algocraft {

struct InstrumentIngestStats {
  std::int64_t rows_read{0};
  std::int64_t rows_kept{0};
  std::int64_t rows_skipped{0};
};

// Parse Upstox complete.csv body (already decompressed). Keeps NSE_EQ EQUITY with INE ISINs.
[[nodiscard]] InstrumentIngestStats parse_upstox_instruments_csv(std::string_view csv,
                                                                 std::vector<InstrumentRow>& out);

// Download https://assets.upstox.com/.../complete.csv.gz (or local .csv / .csv.gz path).
[[nodiscard]] std::string load_upstox_instruments_csv(std::string_view source);

// Full ingest: load → parse → mark inactive → upsert. Returns kept count.
[[nodiscard]] InstrumentIngestStats ingest_upstox_instruments(InstrumentRepository& repo,
                                                             std::string_view source);

inline constexpr std::string_view kUpstoxInstrumentsUrl =
    "https://assets.upstox.com/market-quote/instruments/exchange/complete.csv.gz";

}  // namespace algocraft
