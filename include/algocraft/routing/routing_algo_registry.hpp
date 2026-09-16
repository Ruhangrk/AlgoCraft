#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "algocraft/routing/routing_algo.hpp"

namespace algocraft {

class RoutingAlgoRegistry {
public:
  using Factory = std::function<std::unique_ptr<RoutingAlgo>()>;

  void add(std::string name, Factory factory);
  [[nodiscard]] std::unique_ptr<RoutingAlgo> create(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> names() const;

private:
  std::unordered_map<std::string, Factory> factories_{};
};

void register_all_routers(RoutingAlgoRegistry& registry);

}  // namespace algocraft
