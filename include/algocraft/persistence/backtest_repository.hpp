#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace algocraft {

struct BacktestRow {
  std::int64_t id{};
  std::int64_t workbook_id{};
  std::string strategy_name;
  std::string ticker;
  std::int64_t capital_paise{};
  std::int64_t from_ns{};
  std::int64_t to_ns{};
  std::int64_t ending_equity_paise{};
  std::int64_t pnl_paise{};
  std::int64_t fees_paise{};
  std::int64_t return_pct_bp{};
  std::int64_t max_drawdown_paise{};
  int fills{};
  int bars{};
  std::string status{"completed"};
  std::string created_at;
};

class BacktestRepository {
public:
  explicit BacktestRepository(sqlite3* db);

  // Inserts a completed row; returns new id.
  [[nodiscard]] std::int64_t insert(const BacktestRow& row);

  [[nodiscard]] std::optional<BacktestRow> find(std::int64_t workbook_id,
                                                std::int64_t backtest_id) const;
  [[nodiscard]] std::vector<BacktestRow> list_for_workbook(std::int64_t workbook_id) const;

private:
  sqlite3* db_{nullptr};
};

}  // namespace algocraft
