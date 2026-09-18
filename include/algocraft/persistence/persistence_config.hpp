#pragma once

#include <filesystem>

namespace algocraft {

// Paths for the persistence layer (Thread 2). Not read on Thread 0.
struct PersistenceConfig {
  std::filesystem::path db_path{"data/algocraft.db"};
  std::filesystem::path migrations_dir{"migrations"};
  std::filesystem::path bars_dir{"data/bars"};
};

}  // namespace algocraft
