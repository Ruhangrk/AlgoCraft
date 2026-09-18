#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "algocraft/domain/session_date.hpp"

struct sqlite3;

namespace algocraft {

struct CoverageRow {
  std::string ticker{};
  std::string resolution{};
  std::optional<SessionDate> first_date{};
  std::optional<SessionDate> last_date{};
  std::optional<SessionDate> live_date{};
  std::optional<std::string> last_fetched_at{};
  std::string source{};
  std::int32_t sessions{0};
};

class CoverageRepository {
public:
  explicit CoverageRepository(sqlite3* db);

  [[nodiscard]] std::optional<CoverageRow> get(std::string_view ticker,
                                               std::string_view resolution) const;
  void upsert(const CoverageRow& row);

private:
  sqlite3* db_{nullptr};
};

}  // namespace algocraft
