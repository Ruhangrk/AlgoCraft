#pragma once

#include <filesystem>

#include "algocraft/api/http_helpers.hpp"
#include "algocraft/auth/auth_service.hpp"
#include "algocraft/persistence/persistence_service.hpp"

namespace algocraft::api {

struct AgentRouteDeps {
  AuthService& auth;
  PersistenceService* persist{nullptr};
  std::filesystem::path source_root;
  std::filesystem::path sandbox_root;  // default: <source_root>/data/agent_sandbox
};

void register_agent_routes(App& app, AgentRouteDeps deps);

}  // namespace algocraft::api
