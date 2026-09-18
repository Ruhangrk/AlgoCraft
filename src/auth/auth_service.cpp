#include "algocraft/auth/auth_service.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace algocraft {
namespace {

constexpr int kPbkdf2Iters = 100'000;
constexpr int kSaltLen = 16;
constexpr int kHashLen = 32;

std::string b64url_encode(const unsigned char* data, std::size_t len) {
  static constexpr char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (std::size_t i = 0; i < len; i += 3) {
    const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                           ((i + 1 < len ? static_cast<unsigned int>(data[i + 1]) : 0u) << 8) |
                           (i + 2 < len ? static_cast<unsigned int>(data[i + 2]) : 0u);
    out.push_back(kTable[(n >> 18) & 63]);
    out.push_back(kTable[(n >> 12) & 63]);
    out.push_back(i + 1 < len ? kTable[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? kTable[n & 63] : '=');
  }
  // base64url
  for (char& c : out) {
    if (c == '+') {
      c = '-';
    } else if (c == '/') {
      c = '_';
    }
  }
  while (!out.empty() && out.back() == '=') {
    out.pop_back();
  }
  return out;
}

std::string b64url_encode_str(std::string_view s) {
  return b64url_encode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

std::vector<unsigned char> b64url_decode(std::string_view in) {
  std::string s(in);
  for (char& c : s) {
    if (c == '-') {
      c = '+';
    } else if (c == '_') {
      c = '/';
    }
  }
  while (s.size() % 4 != 0) {
    s.push_back('=');
  }
  static constexpr int kUnused = 0;
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') {
      return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
      return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
      return c - '0' + 52;
    }
    if (c == '+') {
      return 62;
    }
    if (c == '/') {
      return 63;
    }
    return -1;
  };
  (void)kUnused;
  std::vector<unsigned char> out;
  out.reserve(s.size() / 4 * 3);
  for (std::size_t i = 0; i + 3 < s.size(); i += 4) {
    const int a = val(s[i]);
    const int b = val(s[i + 1]);
    const int c = val(s[i + 2]);
    const int d = val(s[i + 3]);
    if (a < 0 || b < 0) {
      break;
    }
    out.push_back(static_cast<unsigned char>((a << 2) | (b >> 4)));
    if (s[i + 2] != '=') {
      out.push_back(static_cast<unsigned char>(((b & 15) << 4) | (c >> 2)));
    }
    if (s[i + 3] != '=') {
      out.push_back(static_cast<unsigned char>(((c & 3) << 6) | d));
    }
  }
  return out;
}

std::string to_hex(const unsigned char* data, std::size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(len * 2, '0');
  for (std::size_t i = 0; i < len; ++i) {
    out[i * 2] = kHex[data[i] >> 4];
    out[i * 2 + 1] = kHex[data[i] & 0xf];
  }
  return out;
}

bool from_hex(std::string_view hex, unsigned char* out, std::size_t out_len) {
  if (hex.size() != out_len * 2) {
    return false;
  }
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  for (std::size_t i = 0; i < out_len; ++i) {
    const int hi = nib(hex[i * 2]);
    const int lo = nib(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

std::int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

AuthService::AuthService(sqlite3* db, std::string jwt_secret, std::int64_t token_ttl_seconds)
    : db_{db}, jwt_secret_{std::move(jwt_secret)}, token_ttl_seconds_{token_ttl_seconds} {
  if (db_ == nullptr) {
    throw std::invalid_argument("AuthService: null db");
  }
  if (jwt_secret_.empty()) {
    throw std::invalid_argument("AuthService: empty jwt secret");
  }
}

std::string AuthService::hash_password(std::string_view password, std::string_view salt_hex) const {
  unsigned char salt[kSaltLen]{};
  if (!from_hex(salt_hex, salt, kSaltLen)) {
    throw std::runtime_error("bad salt");
  }
  unsigned char hash[kHashLen]{};
  if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt, kSaltLen,
                        kPbkdf2Iters, EVP_sha256(), kHashLen, hash) != 1) {
    throw std::runtime_error("pbkdf2 failed");
  }
  return std::string(salt_hex) + "$" + to_hex(hash, kHashLen);
}

bool AuthService::verify_password(std::string_view password, std::string_view stored) const {
  const auto sep = stored.find('$');
  if (sep == std::string_view::npos) {
    return false;
  }
  const auto salt = stored.substr(0, sep);
  const auto expected = stored.substr(sep + 1);
  try {
    const auto got = hash_password(password, salt);
    const auto got_hash = std::string_view{got}.substr(got.find('$') + 1);
    return got_hash.size() == expected.size() &&
           CRYPTO_memcmp(got_hash.data(), expected.data(), expected.size()) == 0;
  } catch (...) {
    return false;
  }
}

std::string AuthService::make_token(const AuthUser& user) const {
  const auto exp = now_unix() + token_ttl_seconds_;
  const std::string header = R"({"alg":"HS256","typ":"JWT"})";
  const std::string payload = "{\"sub\":" + std::to_string(user.id) + ",\"role\":\"" + user.role +
                              "\",\"exp\":" + std::to_string(exp) + "}";
  const std::string signing_input = b64url_encode_str(header) + "." + b64url_encode_str(payload);

  unsigned char mac[EVP_MAX_MD_SIZE]{};
  unsigned int mac_len = 0;
  if (HMAC(EVP_sha256(), jwt_secret_.data(), static_cast<int>(jwt_secret_.size()),
           reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size(), mac,
           &mac_len) == nullptr) {
    throw std::runtime_error("hmac failed");
  }
  return signing_input + "." + b64url_encode(mac, mac_len);
}

AuthService::Result AuthService::register_user(std::string_view username, std::string_view password,
                                               std::string_view role) {
  Result out{};
  if (username.empty() || password.size() < 6) {
    out.error = "invalid username or password";
    return out;
  }
  if (role != "user" && role != "admin") {
    out.error = "invalid role";
    return out;
  }
  if (find_by_username(username)) {
    out.error = "username taken";
    return out;
  }

  unsigned char salt[kSaltLen]{};
  if (RAND_bytes(salt, kSaltLen) != 1) {
    out.error = "rng failed";
    return out;
  }
  const auto salt_hex = to_hex(salt, kSaltLen);
  const auto stored = hash_password(password, salt_hex);

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)", -1,
                         &st, nullptr) != SQLITE_OK) {
    out.error = "db prepare failed";
    return out;
  }
  sqlite3_bind_text(st, 1, username.data(), static_cast<int>(username.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, stored.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, role.data(), static_cast<int>(role.size()), SQLITE_TRANSIENT);
  const int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) {
    out.error = "db insert failed";
    return out;
  }

  out.user.id = sqlite3_last_insert_rowid(db_);
  out.user.username = std::string(username);
  out.user.role = std::string(role);
  out.token = make_token(out.user);
  out.ok = true;
  return out;
}

AuthService::Result AuthService::login(std::string_view username, std::string_view password) {
  Result out{};
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT id, username, password_hash, role FROM users "
                         "WHERE username=? AND deleted_at IS NULL",
                         -1, &st, nullptr) != SQLITE_OK) {
    out.error = "db prepare failed";
    return out;
  }
  sqlite3_bind_text(st, 1, username.data(), static_cast<int>(username.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    out.error = "invalid credentials";
    return out;
  }
  out.user.id = sqlite3_column_int64(st, 0);
  if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 1))) {
    out.user.username = t;
  }
  std::string hash;
  if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 2))) {
    hash = t;
  }
  if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 3))) {
    out.user.role = t;
  }
  sqlite3_finalize(st);

  if (hash == "unset" || !verify_password(password, hash)) {
    out.error = "invalid credentials";
    return out;
  }
  out.token = make_token(out.user);
  out.ok = true;
  return out;
}

std::optional<AuthTokenClaims> AuthService::validate_token(std::string_view token) const {
  const auto p1 = token.find('.');
  if (p1 == std::string_view::npos) {
    return std::nullopt;
  }
  const auto p2 = token.find('.', p1 + 1);
  if (p2 == std::string_view::npos) {
    return std::nullopt;
  }
  const auto signing_input = token.substr(0, p2);
  const auto sig_b64 = token.substr(p2 + 1);

  unsigned char mac[EVP_MAX_MD_SIZE]{};
  unsigned int mac_len = 0;
  if (HMAC(EVP_sha256(), jwt_secret_.data(), static_cast<int>(jwt_secret_.size()),
           reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size(), mac,
           &mac_len) == nullptr) {
    return std::nullopt;
  }
  const auto expected = b64url_encode(mac, mac_len);
  if (expected.size() != sig_b64.size() ||
      CRYPTO_memcmp(expected.data(), sig_b64.data(), expected.size()) != 0) {
    return std::nullopt;
  }

  const auto payload_bytes = b64url_decode(token.substr(p1 + 1, p2 - p1 - 1));
  const std::string payload(payload_bytes.begin(), payload_bytes.end());

  // Minimal JSON field scrape: "sub":N, "role":"...", "exp":N
  AuthTokenClaims claims{};
  auto find_num = [&](std::string_view key) -> std::optional<std::int64_t> {
    const auto pos = payload.find(std::string(key));
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    const auto colon = payload.find(':', pos);
    if (colon == std::string::npos) {
      return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < payload.size() && (payload[i] == ' ' || payload[i] == '"')) {
      ++i;
    }
    char* end = nullptr;
    const auto v = std::strtoll(payload.c_str() + i, &end, 10);
    if (end == payload.c_str() + i) {
      return std::nullopt;
    }
    return v;
  };
  auto find_str = [&](std::string_view key) -> std::optional<std::string> {
    const auto pos = payload.find(std::string(key));
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    const auto colon = payload.find(':', pos);
    const auto q1 = payload.find('"', colon);
    const auto q2 = payload.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) {
      return std::nullopt;
    }
    return payload.substr(q1 + 1, q2 - q1 - 1);
  };

  const auto sub = find_num("\"sub\"");
  const auto exp = find_num("\"exp\"");
  const auto role = find_str("\"role\"");
  if (!sub || !exp || !role) {
    return std::nullopt;
  }
  if (*exp < now_unix()) {
    return std::nullopt;  // expired
  }
  claims.user_id = *sub;
  claims.exp_unix = *exp;
  claims.role = *role;
  return claims;
}

std::optional<AuthUser> AuthService::find_user(std::int64_t id) const {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT id, username, role FROM users WHERE id=? AND deleted_at IS NULL",
                         -1, &st, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_int64(st, 1, id);
  std::optional<AuthUser> out;
  if (sqlite3_step(st) == SQLITE_ROW) {
    AuthUser u{};
    u.id = sqlite3_column_int64(st, 0);
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 1))) {
      u.username = t;
    }
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 2))) {
      u.role = t;
    }
    out = u;
  }
  sqlite3_finalize(st);
  return out;
}

std::optional<AuthUser> AuthService::find_by_username(std::string_view username) const {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(
          db_, "SELECT id, username, role FROM users WHERE username=? AND deleted_at IS NULL", -1,
          &st, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(st, 1, username.data(), static_cast<int>(username.size()), SQLITE_TRANSIENT);
  std::optional<AuthUser> out;
  if (sqlite3_step(st) == SQLITE_ROW) {
    AuthUser u{};
    u.id = sqlite3_column_int64(st, 0);
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 1))) {
      u.username = t;
    }
    if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 2))) {
      u.role = t;
    }
    out = u;
  }
  sqlite3_finalize(st);
  return out;
}

}  // namespace algocraft
