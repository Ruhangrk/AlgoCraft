#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "algocraft/market_data/data_provider.hpp"

namespace algocraft {

class DataSourceRegistry {
public:
  void register_provider(std::unique_ptr<DataProvider> provider);
  void set_active(std::string_view name);

  [[nodiscard]] bool has_active() const { return active_ != nullptr; }
  [[nodiscard]] DataProvider& active_provider();
  [[nodiscard]] const DataProvider& active_provider() const;

private:
  std::unordered_map<std::string, std::unique_ptr<DataProvider>> providers_{};
  DataProvider* active_{nullptr};
};

}  // namespace algocraft
