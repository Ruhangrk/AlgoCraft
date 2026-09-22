#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace algocraft {

// SQLite access for workbook rows used by the HTTP API (Thread 4 only).
class WorkbookRepository {
public:
  explicit WorkbookRepository(sqlite3* db);

  struct Row {
    std::int64_t id{};
    std::string name;
    std::int64_t main_capital_paise{};
    std::int64_t available_paise{};
  };

  struct OwnerRow {
    std::int64_t user_id{};
    std::string name;
    std::int64_t available_paise{};
  };

  // Ensures a users row exists (FK safety), inserts workbook, returns db id.
  // Allocates id via MAX(id)+1 so in-memory WorkbookManager counters cannot collide.
  [[nodiscard]] std::int64_t create(std::int64_t user_id, std::string_view name,
                                    std::int64_t capital_paise, std::string_view username,
                                    std::string_view role);

  [[nodiscard]] std::vector<Row> list_for_user(std::int64_t user_id) const;
  [[nodiscard]] std::optional<OwnerRow> find_owner(std::int64_t workbook_id) const;
  [[nodiscard]] std::optional<Row> find(std::int64_t workbook_id) const;

  // Owner or admin may access; returns false if missing.
  [[nodiscard]] bool can_access(std::int64_t workbook_id, std::int64_t user_id,
                                bool is_admin) const;

  // Sets available_paise for an active workbook. Returns false if missing.
  [[nodiscard]] bool set_available(std::int64_t workbook_id, std::int64_t available_paise);

  // Top-up: main_capital += amount, available += amount, audit capital_added event.
  // Returns updated row; nullopt if workbook missing. Throws on amount <= 0.
  [[nodiscard]] std::optional<Row> add_capital(std::int64_t workbook_id, std::int64_t amount_paise);

private:
  sqlite3* db_{nullptr};
};

}  // namespace algocraft
