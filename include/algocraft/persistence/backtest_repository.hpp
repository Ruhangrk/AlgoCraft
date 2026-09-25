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

struct BacktestListFilter {
  std::string from_date;   // YYYY-MM-DD inclusive on created_at
  std::string to_date;
  int limit{0};            // 0 = no LIMIT
  std::int64_t cursor{0};  // return id < cursor
};

struct BacktestSignalRow {
  std::int64_t id{};
  std::string ticker;
  std::string strategy_name;
  int intent_count{1};
  std::string indicators_json;
  std::int64_t timestamp_ns{};
};

struct BacktestRejectionRow {
  std::int64_t id{};
  std::string ticker;
  std::string rule_name;
  std::string reason;
  std::int64_t timestamp_ns{};
};

struct BacktestFillRow {
  std::int64_t id{};
  std::string ticker;
  std::string side;
  std::int64_t qty{};
  std::int64_t price_paise{};
  std::int64_t fees_paise{};
  std::int64_t timestamp_ns{};
};

struct BacktestEventBatch {
  std::vector<BacktestSignalRow> signals;
  std::vector<BacktestRejectionRow> rejections;
  std::vector<BacktestFillRow> fills;
};

class BacktestRepository {
public:
  explicit BacktestRepository(sqlite3* db);

  // Inserts a completed row; returns new id.
  [[nodiscard]] std::int64_t insert(const BacktestRow& row);

  void insert_events(std::int64_t workbook_id, std::int64_t backtest_id,
                     const BacktestEventBatch& events);

  [[nodiscard]] std::optional<BacktestRow> find(std::int64_t workbook_id,
                                                std::int64_t backtest_id) const;
  [[nodiscard]] std::vector<BacktestRow> list_for_workbook(std::int64_t workbook_id) const {
    return list_for_workbook(workbook_id, BacktestListFilter{});
  }
  [[nodiscard]] std::vector<BacktestRow> list_for_workbook(std::int64_t workbook_id,
                                                           const BacktestListFilter& filter) const;
  // Soft-delete: sets deleted_at. Returns false if missing/already deleted.
  [[nodiscard]] bool soft_delete(std::int64_t workbook_id, std::int64_t backtest_id);

  [[nodiscard]] std::vector<BacktestSignalRow> list_signals(std::int64_t backtest_id) const;
  [[nodiscard]] std::vector<BacktestRejectionRow> list_rejections(std::int64_t backtest_id) const;
  [[nodiscard]] std::vector<BacktestFillRow> list_fills(std::int64_t backtest_id) const;

private:
  sqlite3* db_{nullptr};
};

}  // namespace algocraft
