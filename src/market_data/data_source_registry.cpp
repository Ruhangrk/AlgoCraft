#include "algocraft/market_data/data_source_registry.hpp"

#include <stdexcept>
#include <utility>

namespace algocraft {

void DataSourceRegistry::register_provider(std::unique_ptr<DataProvider> provider) {
  if (!provider) {
    throw std::invalid_argument("provider is null");
  }

  const std::string key{provider->name()};
  if (providers_.contains(key)) {
    throw std::invalid_argument("provider already registered: " + key);
  }

  const auto it = providers_.emplace(key, std::move(provider)).first;
  if (active_ == nullptr) {
    active_ = it->second.get();
  }
}

void DataSourceRegistry::set_active(std::string_view name) {
  const std::string key{name};
  const auto it = providers_.find(key);
  if (it == providers_.end()) {
    throw std::invalid_argument("unknown data source: " + key);
  }
  active_ = it->second.get();
}

DataProvider& DataSourceRegistry::active_provider() {
  if (active_ == nullptr) {
    throw std::logic_error("no active data provider");
  }
  return *active_;
}

const DataProvider& DataSourceRegistry::active_provider() const {
  if (active_ == nullptr) {
    throw std::logic_error("no active data provider");
  }
  return *active_;
}

}  // namespace algocraft
