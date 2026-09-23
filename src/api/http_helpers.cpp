#include "algocraft/api/http_helpers.hpp"

#include <spdlog/spdlog.h>

namespace algocraft::api {

void CorsMiddleware::before_handle(crow::request& req, crow::response& res, context&) {
  spdlog::info("http {} {}", crow::method_name(req.method), req.raw_url);
  if (req.method == "OPTIONS"_method) {
    res.code = 204;
    res.add_header("Access-Control-Allow-Origin", "*");
    res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.end();
  }
}

void CorsMiddleware::after_handle(crow::request&, crow::response& res, context&) {
  res.add_header("Access-Control-Allow-Origin", "*");
  res.add_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
  res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

crow::response json_response(int code, std::string body) {
  crow::response res{code, std::move(body)};
  res.set_header("Content-Type", "application/json");
  return res;
}

crow::response json_ok(crow::json::wvalue root, int code) {
  return json_response(code, root.dump());
}

crow::response json_error(int code, std::string_view msg) {
  crow::json::wvalue root;
  root["error"] = std::string(msg);
  return json_response(code, root.dump());
}

crow::json::rvalue body_or_empty(const crow::request& req) {
  auto body = crow::json::load(req.body);
  if (!body) {
    body = crow::json::load("{}");
  }
  return body;
}

std::string json_string_or(const crow::json::rvalue& body, const char* key,
                           std::string fallback) {
  if (!body.has(key) || body[key].t() != crow::json::type::String) {
    return fallback;
  }
  return std::string(body[key].s());
}

std::int64_t json_int_or(const crow::json::rvalue& body, const char* key, std::int64_t fallback) {
  if (!body.has(key) || body[key].t() != crow::json::type::Number) {
    return fallback;
  }
  return body[key].i();
}

std::vector<std::string> json_string_array(const crow::json::rvalue& body, const char* key) {
  std::vector<std::string> out;
  if (!body.has(key) || body[key].t() != crow::json::type::List) {
    return out;
  }
  for (const auto& item : body[key]) {
    if (item.t() == crow::json::type::String) {
      out.emplace_back(std::string(item.s()));
    }
  }
  return out;
}

bool read_username_password(const crow::json::rvalue& body, std::string& user, std::string& pass) {
  if (!body || !body.has("username") || !body.has("password") ||
      body["username"].t() != crow::json::type::String ||
      body["password"].t() != crow::json::type::String) {
    return false;
  }
  user = std::string(body["username"].s());
  pass = std::string(body["password"].s());
  return true;
}

std::optional<std::string> bearer_token(const crow::request& req) {
  auto auth = req.get_header_value("Authorization");
  if (auth.empty()) {
    // Crow returns nullptr (not "") when the query key is absent.
    const char* q = req.url_params.get("token");
    if (q != nullptr && *q != '\0') {
      return std::string{q};
    }
    return std::nullopt;
  }
  constexpr std::string_view kPrefix = "Bearer ";
  if (auth.size() <= kPrefix.size()) {
    return std::nullopt;
  }
  if (auth.compare(0, kPrefix.size(), "Bearer ") != 0 &&
      auth.compare(0, kPrefix.size(), "bearer ") != 0) {
    return std::nullopt;
  }
  return auth.substr(kPrefix.size());
}

AuthGate require_user(AuthService& auth, const crow::request& req) {
  const auto tok = bearer_token(req);
  if (!tok) {
    return AuthGate{std::nullopt, json_error(401, "missing token")};
  }
  auto claims = auth.validate_token(*tok);
  if (!claims) {
    return AuthGate{std::nullopt, json_error(401, "invalid or expired token")};
  }
  return AuthGate{std::move(claims), {}};
}

AuthGate require_workbook(AuthService& auth, WorkbookRepository& workbooks,
                          const crow::request& req, std::int64_t wid) {
  auto gate = require_user(auth, req);
  if (!gate) {
    return gate;
  }
  if (!workbooks.can_access(wid, gate.claims->user_id, gate.claims->role == "admin")) {
    const bool exists = workbooks.find(wid).has_value();
    return AuthGate{std::nullopt,
                    json_error(exists ? 403 : 404, exists ? "forbidden" : "workbook not found")};
  }
  return gate;
}

}  // namespace algocraft::api
