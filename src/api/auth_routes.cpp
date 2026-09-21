#include "algocraft/api/auth_routes.hpp"

namespace algocraft::api {
namespace {

crow::json::wvalue auth_result_json(const AuthService::Result& result) {
  crow::json::wvalue root;
  root["token"] = result.token;
  root["user"]["id"] = result.user.id;
  root["user"]["username"] = result.user.username;
  root["user"]["role"] = result.user.role;
  return root;
}

crow::json::wvalue user_json(const AuthUser& user) {
  crow::json::wvalue root;
  root["id"] = user.id;
  root["username"] = user.username;
  root["role"] = user.role;
  return root;
}

}  // namespace

void register_auth_routes(App& app, AuthService& auth) {
  CROW_ROUTE(app, "/auth/register")
      .methods(crow::HTTPMethod::POST)([&auth](const crow::request& req) {
        std::string user;
        std::string pass;
        if (!read_username_password(body_or_empty(req), user, pass)) {
          return json_error(400, "username and password required");
        }
        const auto result = auth.register_user(user, pass);
        if (!result.ok) {
          return json_error(400, result.error);
        }
        return json_ok(auth_result_json(result));
      });

  CROW_ROUTE(app, "/auth/login")
      .methods(crow::HTTPMethod::POST)([&auth](const crow::request& req) {
        std::string user;
        std::string pass;
        if (!read_username_password(body_or_empty(req), user, pass)) {
          return json_error(400, "username and password required");
        }
        const auto result = auth.login(user, pass);
        if (!result.ok) {
          return json_error(401, result.error);
        }
        return json_ok(auth_result_json(result));
      });

  CROW_ROUTE(app, "/auth/me")
      .methods(crow::HTTPMethod::GET)([&auth](const crow::request& req) {
        auto gate = require_user(auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto user = auth.find_user(gate.claims->user_id);
        if (!user) {
          return json_error(404, "user not found");
        }
        return json_ok(user_json(*user));
      });
}

}  // namespace algocraft::api
