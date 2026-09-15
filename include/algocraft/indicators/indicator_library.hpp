#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/indicators/indicator.hpp"

namespace algocraft {

class IndicatorLibrary {
public:
  template <typename T, typename... Args>
  T& get(SymbolId symbol_id, BarResolution resolution, Args&&... args) {
    std::ostringstream key;
    key << T::kTypeName << ':' << symbol_id << ':' << static_cast<int>(resolution);
    append_args(key, args...);

    const auto s = key.str();
    if (auto it = by_key_.find(s); it != by_key_.end()) {
      return *static_cast<T*>(it->second.get());
    }

    auto owned = std::make_unique<T>(std::forward<Args>(args)...);
    T& ref = *owned;
    by_symbol_[symbol_id].push_back(owned.get());
    by_key_.emplace(s, std::move(owned));
    return ref;
  }

  void update(const BarEvent& bar) {
    const auto it = by_symbol_.find(bar.symbol_id);
    if (it == by_symbol_.end()) {
      return;
    }
    for (Indicator* indicator : it->second) {
      indicator->update(bar);
    }
  }

private:
  static void append_args(std::ostringstream&) {}

  template <typename First, typename... Rest>
  static void append_args(std::ostringstream& key, First&& first, Rest&&... rest) {
    key << ':' << first;
    append_args(key, std::forward<Rest>(rest)...);
  }

  std::unordered_map<std::string, std::unique_ptr<Indicator>> by_key_{};
  std::unordered_map<SymbolId, std::vector<Indicator*>> by_symbol_{};
};

}  // namespace algocraft
