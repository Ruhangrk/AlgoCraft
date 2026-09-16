#include "algocraft/routing/routing_algo_registry.hpp"

#include "algocraft/routing/default_router.hpp"

#include <stdexcept>

namespace algocraft {

void RoutingAlgoRegistry::add(std::string name, Factory factory) {
  factories_.emplace(std::move(name), std::move(factory));
}

std::unique_ptr<RoutingAlgo> RoutingAlgoRegistry::create(std::string_view name) const {
  const auto it = factories_.find(std::string{name});
  if (it == factories_.end()) {
    throw std::invalid_argument("unknown routing algo: " + std::string{name});
  }
  return it->second();
}

std::vector<std::string> RoutingAlgoRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(factories_.size());
  for (const auto& [name, _] : factories_) {
    out.push_back(name);
  }
  return out;
}

void register_all_routers(RoutingAlgoRegistry& registry) {
  registry.add("default_router", [] { return std::make_unique<DefaultRouter>(); });
}

}  // namespace algocraft
