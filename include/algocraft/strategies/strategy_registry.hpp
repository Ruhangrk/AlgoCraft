#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "algocraft/strategies/strategy.hpp"

namespace algocraft {

class StrategyRegistry {
public:
  using Factory = std::function<std::unique_ptr<Strategy>()>;

  void add(std::string name, Factory factory);

  [[nodiscard]] std::unique_ptr<Strategy> create(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> names() const;

private:
  std::unordered_map<std::string, Factory> factories_{};
};

void register_all_strategies(StrategyRegistry& registry);

}  // namespace algocraft
