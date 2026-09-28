#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace algocraft {

class StrategyCatalogRepository {
public:
  explicit StrategyCatalogRepository(sqlite3* db);

  struct Row {
    std::int64_t id{};
    std::string kind{"strategy"};
    std::string name;
    std::string class_name;
    bool enabled{false};
    std::string hpp_path;
    std::string cpp_path;
    std::string sandbox_path;
    std::string provenance{"agent"};
    bool compile_ok{false};
    std::string created_at;
    std::string updated_at;
  };

  // Insert or update by name. Always leaves enabled=0 on promote upsert unless set_enabled called.
  [[nodiscard]] std::int64_t upsert_promoted(const Row& row);

  [[nodiscard]] bool set_enabled(std::string_view name, bool enabled);
  [[nodiscard]] bool set_compile_ok(std::string_view name, bool ok, std::string_view sandbox_path);

  [[nodiscard]] std::optional<Row> find_by_name(std::string_view name) const;
  [[nodiscard]] std::vector<Row> list_all() const;
  [[nodiscard]] std::vector<Row> list_enabled() const;

private:
  sqlite3* db_{nullptr};
};

}  // namespace algocraft
