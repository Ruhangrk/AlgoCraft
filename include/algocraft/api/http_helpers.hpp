#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <crow.h>

#include "algocraft/auth/auth_service.hpp"
#include "algocraft/persistence/workbook_repository.hpp"

namespace algocraft::api {

struct CorsMiddleware {
  struct context {};

  void before_handle(crow::request& req, crow::response& res, context&);
  void after_handle(crow::request&, crow::response& res, context&);
};

using App = crow::App<CorsMiddleware>;

struct AuthGate {
  std::optional<AuthTokenClaims> claims;
  crow::response error;
  explicit operator bool() const { return claims.has_value(); }
};

crow::response json_response(int code, std::string body);
crow::response json_ok(crow::json::wvalue root, int code = 200);
crow::response json_error(int code, std::string_view msg);

crow::json::rvalue body_or_empty(const crow::request& req);
std::string json_string_or(const crow::json::rvalue& body, const char* key, std::string fallback);
std::int64_t json_int_or(const crow::json::rvalue& body, const char* key, std::int64_t fallback);
std::vector<std::string> json_string_array(const crow::json::rvalue& body, const char* key);
bool read_username_password(const crow::json::rvalue& body, std::string& user, std::string& pass);

std::optional<std::string> bearer_token(const crow::request& req);
AuthGate require_user(AuthService& auth, const crow::request& req);
AuthGate require_workbook(AuthService& auth, WorkbookRepository& workbooks,
                          const crow::request& req, std::int64_t wid);

}  // namespace algocraft::api
