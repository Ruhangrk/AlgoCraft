#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace algocraft {

struct InstrumentRow {
  std::string ticker;
  std::string name;
  std::string isin;
  std::string exchange{"NSE"};
  std::string segment{"EQ"};
  std::int64_t lot_size{1};
  std::int64_t tick_size_paise{5};
  std::string instrument_key;
  bool active{true};
  std::string updated_at;
};

// SQLite catalog of NSE EQ (and later) instruments. Thread 4 / CLI only.
class InstrumentRepository {
public:
  explicit InstrumentRepository(sqlite3* db);

  void upsert(const InstrumentRow& row);
  void upsert_many(const std::vector<InstrumentRow>& rows);
  void mark_all_inactive();

  [[nodiscard]] std::optional<InstrumentRow> find_by_ticker(std::string_view ticker) const;
  [[nodiscard]] std::vector<InstrumentRow> search(std::string_view query, int limit) const;
  [[nodiscard]] std::int64_t count_active() const;

private:
  sqlite3* db_{nullptr};
};

}  // namespace algocraft
