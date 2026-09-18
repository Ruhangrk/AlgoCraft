#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;

namespace algocraft {

struct AuthUser {
  std::int64_t id{0};
  std::string username;
  std::string role{"user"};  // user | admin
};

struct AuthTokenClaims {
  std::int64_t user_id{0};
  std::string role;
  std::int64_t exp_unix{0};
};

class AuthService {
public:
  AuthService(sqlite3* db, std::string jwt_secret, std::int64_t token_ttl_seconds = 86400);

  // Returns JWT on success; error message on failure.
  struct Result {
    bool ok{false};
    std::string token;
    AuthUser user;
    std::string error;
  };

  Result register_user(std::string_view username, std::string_view password,
                       std::string_view role = "user");
  Result login(std::string_view username, std::string_view password);

  [[nodiscard]] std::optional<AuthTokenClaims> validate_token(std::string_view token) const;
  [[nodiscard]] std::optional<AuthUser> find_user(std::int64_t id) const;
  [[nodiscard]] std::optional<AuthUser> find_by_username(std::string_view username) const;

private:
  sqlite3* db_{nullptr};
  std::string jwt_secret_;
  std::int64_t token_ttl_seconds_{86400};

  [[nodiscard]] std::string hash_password(std::string_view password, std::string_view salt) const;
  [[nodiscard]] bool verify_password(std::string_view password, std::string_view stored) const;
  [[nodiscard]] std::string make_token(const AuthUser& user) const;
};

}  // namespace algocraft
