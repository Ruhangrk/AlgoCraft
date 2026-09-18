#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;

namespace algocraft {

// Applies ordered schema_*.sql files. Records versions in schema_migrations.
class MigrationRunner {
public:
  MigrationRunner(sqlite3* db, std::filesystem::path migrations_dir);

  void apply();
  [[nodiscard]] std::vector<std::string> applied() const;

private:
  void ensure_version_table() const;
  void apply_file(const std::filesystem::path& path) const;
  [[nodiscard]] bool is_applied(const std::string& filename) const;
  [[nodiscard]] std::vector<std::filesystem::path> list_files() const;

  sqlite3* db_{nullptr};
  std::filesystem::path migrations_dir_{};
};

}  // namespace algocraft
