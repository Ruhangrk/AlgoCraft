#pragma once

#include "algocraft/api/http_helpers.hpp"
#include "algocraft/auth/auth_service.hpp"

namespace algocraft::api {

void register_auth_routes(App& app, AuthService& auth);

}  // namespace algocraft::api
