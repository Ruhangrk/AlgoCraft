#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "algocraft/persistence/persistence_config.hpp"

struct sqlite3;

namespace algocraft {

// SQLite connection for the persistence layer. Thread 0 / 1 must not open this.
class SqliteDatabase {
public:
  explicit SqliteDatabase(PersistenceConfig config);
  ~SqliteDatabase();

  SqliteDatabase(const SqliteDatabase&) = delete;
  SqliteDatabase& operator=(const SqliteDatabase&) = delete;
  SqliteDatabase(SqliteDatabase&&) = delete;
  SqliteDatabase& operator=(SqliteDatabase&&) = delete;

  void open();
  void close();
  void migrate();

  void execute(std::string_view sql);

  [[nodiscard]] bool is_open() const { return db_ != nullptr; }
  [[nodiscard]] const std::filesystem::path& path() const { return config_.db_path; }
  [[nodiscard]] const std::string& journal_mode() const { return journal_mode_; }
  [[nodiscard]] std::vector<std::string> table_names() const;
  [[nodiscard]] std::vector<std::string> applied_migrations() const;
  [[nodiscard]] sqlite3* handle() const { return db_.get(); }

private:
  struct Closer {
    void operator()(sqlite3* db) const noexcept;
  };

  void apply_pragmas();
  void ensure_open() const;

  PersistenceConfig config_{};
  std::unique_ptr<sqlite3, Closer> db_{};
  std::string journal_mode_{};
};

}  // namespace algocraft
