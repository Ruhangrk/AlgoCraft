#include "algocraft/api/http_server.hpp"

#include <sqlite3.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/instrument.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/engine/run_manager.hpp"

namespace algocraft {
namespace {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
  std::string authorization;
};

struct HttpResponse {
  int status{200};
  std::string body{R"({"ok":true})"};
  std::string content_type{"application/json"};
};

std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

std::optional<std::string> json_string_field(std::string_view body, std::string_view key) {
  const auto needle = std::string("\"") + std::string(key) + "\"";
  const auto pos = body.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = body.find(':', pos);
  const auto q1 = body.find('"', colon);
  if (q1 == std::string_view::npos) {
    return std::nullopt;
  }
  const auto q2 = body.find('"', q1 + 1);
  if (q2 == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(body.substr(q1 + 1, q2 - q1 - 1));
}

std::optional<std::int64_t> json_int_field(std::string_view body, std::string_view key) {
  const auto needle = std::string("\"") + std::string(key) + "\"";
  const auto pos = body.find(needle);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = body.find(':', pos);
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t i = colon + 1;
  while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) {
    ++i;
  }
  char* end = nullptr;
  const auto v = std::strtoll(body.data() + static_cast<std::ptrdiff_t>(i), &end, 10);
  if (end == body.data() + static_cast<std::ptrdiff_t>(i)) {
    return std::nullopt;
  }
  return v;
}

std::vector<std::string> json_string_array(std::string_view body, std::string_view key) {
  std::vector<std::string> out;
  const auto needle = std::string("\"") + std::string(key) + "\"";
  const auto pos = body.find(needle);
  if (pos == std::string_view::npos) {
    return out;
  }
  const auto lb = body.find('[', pos);
  const auto rb = body.find(']', lb);
  if (lb == std::string_view::npos || rb == std::string_view::npos) {
    return out;
  }
  auto chunk = body.substr(lb + 1, rb - lb - 1);
  std::size_t i = 0;
  while (i < chunk.size()) {
    const auto q1 = chunk.find('"', i);
    if (q1 == std::string_view::npos) {
      break;
    }
    const auto q2 = chunk.find('"', q1 + 1);
    if (q2 == std::string_view::npos) {
      break;
    }
    out.emplace_back(chunk.substr(q1 + 1, q2 - q1 - 1));
    i = q2 + 1;
  }
  return out;
}

std::optional<std::string> bearer_token(const HttpRequest& req) {
  constexpr std::string_view kPrefix = "Bearer ";
  auto auth = req.authorization;
  while (!auth.empty() && (auth.front() == ' ' || auth.front() == '\t')) {
    auth.erase(auth.begin());
  }
  if (auth.size() <= kPrefix.size()) {
    return std::nullopt;
  }
  if (auth.compare(0, kPrefix.size(), "Bearer ") != 0 &&
      auth.compare(0, kPrefix.size(), "bearer ") != 0) {
    return std::nullopt;
  }
  return auth.substr(kPrefix.size());
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

void normalize_path(HttpRequest& req) {
  const auto q = req.path.find('?');
  if (q != std::string::npos) {
    req.path.resize(q);
  }
  if (req.path.size() >= 4 && req.path.compare(0, 4, "/api") == 0) {
    if (req.path.size() == 4) {
      req.path = "/";
    } else if (req.path[4] == '/') {
      req.path.erase(0, 4);
    }
  }
}

const char* reason_phrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 503:
      return "Service Unavailable";
    default:
      return "OK";
  }
}

bool read_request(int fd, HttpRequest& out) {
  std::string raw;
  raw.reserve(4096);
  char buf[2048];
  while (raw.find("\r\n\r\n") == std::string::npos) {
    const auto n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return false;
    }
    raw.append(buf, static_cast<std::size_t>(n));
    if (raw.size() > 1'000'000) {
      return false;
    }
  }
  const auto header_end = raw.find("\r\n\r\n");
  const auto headers = raw.substr(0, header_end);
  std::size_t content_length = 0;
  {
    std::istringstream hs(headers);
    std::string line;
    if (!std::getline(hs, line)) {
      return false;
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::istringstream ls(line);
    ls >> out.method >> out.path;
    while (std::getline(hs, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      auto name = ascii_lower(line.substr(0, colon));
      auto value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
      }
      if (name == "content-length") {
        content_length = static_cast<std::size_t>(std::stoul(value));
      } else if (name == "authorization") {
        out.authorization = value;
      }
    }
  }
  normalize_path(out);
  out.body = raw.substr(header_end + 4);
  while (out.body.size() < content_length) {
    const auto n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      break;
    }
    out.body.append(buf, static_cast<std::size_t>(n));
  }
  if (out.body.size() > content_length) {
    out.body.resize(content_length);
  }
  return true;
}

void write_response(int fd, const HttpResponse& res) {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << res.status << " " << reason_phrase(res.status) << "\r\n"
      << "Content-Type: " << res.content_type << "\r\n"
      << "Content-Length: " << res.body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
      << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
      << "Connection: close\r\n\r\n"
      << res.body;
  const auto s = oss.str();
  ::send(fd, s.data(), s.size(), 0);
}

std::int64_t uuid_low(const Uuid& id) {
  std::uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val = (val << 8) | id.bytes[static_cast<std::size_t>(8 + i)];
  }
  return static_cast<std::int64_t>(val);
}

Timestamp parse_ymd_ist_midnight(std::string_view ymd) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(ymd.data(), "%d-%d-%d", &y, &m, &d) != 3) {
    throw std::runtime_error("bad date");
  }
  using namespace std::chrono;
  const auto utc = sys_days{year{y} / m / d} + hours{0} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

Timestamp parse_ymd_ist_eod(std::string_view ymd) {
  int y = 0, m = 0, d = 0;
  if (std::sscanf(ymd.data(), "%d-%d-%d", &y, &m, &d) != 3) {
    throw std::runtime_error("bad date");
  }
  using namespace std::chrono;
  const auto utc = sys_days{year{y} / m / d} + hours{23} + minutes{59} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

}  // namespace

HttpServer::HttpServer(Config config, SqliteDatabase& db, DataSourceRegistry& data,
                       StrategyRegistry& strategies, SymbolTable& symbols, DataFetchService* fetch)
    : config_{std::move(config)},
      db_{db},
      data_{data},
      strategies_{strategies},
      symbols_{symbols},
      fetch_{fetch},
      auth_{db.handle(), config_.jwt_secret},
      activity_{db.handle()} {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::stop() {
  running_ = false;
  if (server_ != nullptr) {
    const auto fd = *static_cast<int*>(server_);
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
    delete static_cast<int*>(server_);
    server_ = nullptr;
  }
  if (thread_ && thread_->joinable()) {
    thread_->join();
  }
  thread_.reset();
}

void HttpServer::start_async() {
  thread_ = std::make_unique<std::thread>([this] { start(); });
}

void HttpServer::start() {
  const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    throw std::runtime_error("socket failed");
  }
  int yes = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config_.port));
  if (::inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
    ::close(listen_fd);
    throw std::runtime_error("bad host");
  }
  if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(listen_fd);
    throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
  }
  if (::listen(listen_fd, 16) < 0) {
    ::close(listen_fd);
    throw std::runtime_error("listen failed");
  }
  server_ = new int{listen_fd};
  running_ = true;

  auto require_user = [this](const HttpRequest& req, HttpResponse& res) -> std::optional<AuthTokenClaims> {
    const auto tok = bearer_token(req);
    if (!tok) {
      res.status = 401;
      res.body = R"({"error":"missing token"})";
      return std::nullopt;
    }
    auto claims = auth_.validate_token(*tok);
    if (!claims) {
      res.status = 401;
      res.body = R"({"error":"invalid or expired token"})";
      return std::nullopt;
    }
    return claims;
  };

  auto handle = [&](const HttpRequest& req) -> HttpResponse {
    HttpResponse res;

    if (req.method == "OPTIONS") {
      res.status = 204;
      res.body.clear();
      return res;
    }

    if (req.method == "POST" && req.path == "/auth/register") {
      const auto user = json_string_field(req.body, "username");
      const auto pass = json_string_field(req.body, "password");
      if (!user || !pass) {
        res.status = 400;
        res.body = R"({"error":"username and password required"})";
        return res;
      }
      const auto result = auth_.register_user(*user, *pass);
      if (!result.ok) {
        res.status = 400;
        res.body = "{\"error\":\"" + json_escape(result.error) + "\"}";
        return res;
      }
      res.body = "{\"token\":\"" + json_escape(result.token) + "\",\"user\":{\"id\":" +
                 std::to_string(result.user.id) + ",\"username\":\"" +
                 json_escape(result.user.username) + "\",\"role\":\"" +
                 json_escape(result.user.role) + "\"}}";
      return res;
    }

    if (req.method == "POST" && req.path == "/auth/login") {
      const auto user = json_string_field(req.body, "username");
      const auto pass = json_string_field(req.body, "password");
      if (!user || !pass) {
        res.status = 400;
        res.body = R"({"error":"username and password required"})";
        return res;
      }
      const auto result = auth_.login(*user, *pass);
      if (!result.ok) {
        res.status = 401;
        res.body = "{\"error\":\"" + json_escape(result.error) + "\"}";
        return res;
      }
      res.body = "{\"token\":\"" + json_escape(result.token) + "\",\"user\":{\"id\":" +
                 std::to_string(result.user.id) + ",\"username\":\"" +
                 json_escape(result.user.username) + "\",\"role\":\"" +
                 json_escape(result.user.role) + "\"}}";
      return res;
    }

    if (req.method == "GET" && req.path == "/auth/me") {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto user = auth_.find_user(claims->user_id);
      if (!user) {
        res.status = 404;
        res.body = R"({"error":"user not found"})";
        return res;
      }
      res.body = "{\"id\":" + std::to_string(user->id) + ",\"username\":\"" +
                 json_escape(user->username) + "\",\"role\":\"" + json_escape(user->role) + "\"}";
      return res;
    }

    if (req.method == "GET" && req.path == "/strategies") {
      const auto names = strategies_.names();
      std::ostringstream oss;
      oss << '[';
      for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) {
          oss << ',';
        }
        oss << '"' << json_escape(names[i]) << '"';
      }
      oss << ']';
      res.body = oss.str();
      return res;
    }

    if (req.method == "GET" && req.path == "/routing-algos") {
      res.body = R"(["default_router"])";
      return res;
    }

    if (req.method == "POST" && req.path == "/market-data/ensure") {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      if (fetch_ == nullptr) {
        res.status = 503;
        res.body = R"({"error":"market-data fetch not configured"})";
        return res;
      }
      auto tickers = json_string_array(req.body, "tickers");
      if (tickers.empty()) {
        // First 5 of DefaultRouter universe
        tickers = {"RELIANCE", "INFY", "TCS", "HDFCBANK", "ICICIBANK"};
      }
      const auto from_s = json_string_field(req.body, "from").value_or("2026-08-18");
      const auto to_s = json_string_field(req.body, "to").value_or("2026-09-11");
      Timestamp from_ts{};
      Timestamp to_ts{};
      try {
        from_ts = parse_ymd_ist_midnight(from_s);
        to_ts = parse_ymd_ist_eod(to_s);
      } catch (...) {
        res.status = 400;
        res.body = R"({"error":"from/to must be YYYY-MM-DD"})";
        return res;
      }

      std::ostringstream oss;
      oss << "{\"source\":\"" << json_escape(std::string{data_.active_provider().name()})
          << "\",\"results\":[";
      bool first = true;
      const auto before = fetch_->vendor_fetches();
      for (const auto& ticker : tickers) {
        symbols_.intern({.ticker = ticker}, {});
        std::string err;
        try {
          fetch_->ensure_data_available(ticker, from_ts, to_ts, BarResolution::OneMin);
        } catch (const std::exception& e) {
          err = e.what();
        }
        if (!first) {
          oss << ',';
        }
        first = false;
        oss << "{\"ticker\":\"" << json_escape(ticker) << "\",\"ok\":"
            << (err.empty() ? "true" : "false");
        if (!err.empty()) {
          oss << ",\"error\":\"" << json_escape(err) << '"';
        }
        oss << '}';
      }
      oss << "],\"vendor_fetches\":" << (fetch_->vendor_fetches() - before) << '}';
      res.body = oss.str();
      return res;
    }

    if (req.method == "POST" && req.path == "/workbooks") {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto name = json_string_field(req.body, "name").value_or("workbook");
      const auto capital = json_int_field(req.body, "capital_paise").value_or(10'00'000'00);
      const auto uid = UserId::from_u64(static_cast<std::uint64_t>(claims->user_id));
      const auto wid = books_.create(uid, name, Capital::from_paise(capital));
      const auto wb_val = uuid_low(wid);

      sqlite3* h = db_.handle();
      sqlite3_exec(h, "BEGIN", nullptr, nullptr, nullptr);
      {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(h,
                           "INSERT OR IGNORE INTO users (id, username, password_hash, role) "
                           "VALUES (?, ?, 'unset', ?)",
                           -1, &st, nullptr);
        const auto uname = auth_.find_user(claims->user_id);
        const std::string username = uname ? uname->username : ("user_" + std::to_string(claims->user_id));
        const std::string role = uname ? uname->role : "user";
        sqlite3_bind_int64(st, 1, claims->user_id);
        sqlite3_bind_text(st, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
      }
      {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(h,
                           "INSERT INTO workbooks (id, user_id, name, main_capital_paise, "
                           "available_paise) VALUES (?, ?, ?, ?, ?)",
                           -1, &st, nullptr);
        sqlite3_bind_int64(st, 1, wb_val);
        sqlite3_bind_int64(st, 2, claims->user_id);
        sqlite3_bind_text(st, 3, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 4, capital);
        sqlite3_bind_int64(st, 5, capital);
        sqlite3_step(st);
        sqlite3_finalize(st);
      }
      sqlite3_exec(h, "COMMIT", nullptr, nullptr, nullptr);

      res.status = 201;
      res.body = "{\"id\":" + std::to_string(wb_val) + ",\"name\":\"" + json_escape(name) +
                 "\",\"capital_paise\":" + std::to_string(capital) + "}";
      return res;
    }

    if (req.method == "GET" && req.path == "/workbooks") {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      sqlite3_stmt* st = nullptr;
      sqlite3_prepare_v2(db_.handle(),
                         "SELECT id, name, main_capital_paise, available_paise FROM workbooks "
                         "WHERE user_id=? AND deleted_at IS NULL ORDER BY id",
                         -1, &st, nullptr);
      sqlite3_bind_int64(st, 1, claims->user_id);
      std::ostringstream oss;
      oss << '[';
      bool first = true;
      while (sqlite3_step(st) == SQLITE_ROW) {
        if (!first) {
          oss << ',';
        }
        first = false;
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        oss << "{\"id\":" << sqlite3_column_int64(st, 0) << ",\"name\":\""
            << json_escape(name ? name : "") << "\",\"main_capital_paise\":"
            << sqlite3_column_int64(st, 2) << ",\"available_paise\":"
            << sqlite3_column_int64(st, 3) << '}';
      }
      sqlite3_finalize(st);
      oss << ']';
      res.body = oss.str();
      return res;
    }

    std::smatch m;
    const std::regex re_runs_start(R"(/workbooks/(\d+)/runs/start)");
    if (req.method == "POST" && std::regex_match(req.path, m, re_runs_start)) {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto wid = std::stoll(m[1].str());
      sqlite3_stmt* chk = nullptr;
      sqlite3_prepare_v2(db_.handle(),
                         "SELECT user_id, name, available_paise FROM workbooks "
                         "WHERE id=? AND deleted_at IS NULL",
                         -1, &chk, nullptr);
      sqlite3_bind_int64(chk, 1, wid);
      if (sqlite3_step(chk) != SQLITE_ROW) {
        sqlite3_finalize(chk);
        res.status = 404;
        res.body = R"({"error":"workbook not found"})";
        return res;
      }
      const auto owner = sqlite3_column_int64(chk, 0);
      std::string wb_name;
      if (auto* t = reinterpret_cast<const char*>(sqlite3_column_text(chk, 1))) {
        wb_name = t;
      }
      const auto avail = sqlite3_column_int64(chk, 2);
      sqlite3_finalize(chk);
      if (owner != claims->user_id && claims->role != "admin") {
        res.status = 403;
        res.body = R"({"error":"forbidden"})";
        return res;
      }

      RunConfig cfg{};
      cfg.user_id = UserId::from_u64(static_cast<std::uint64_t>(claims->user_id));
      cfg.workbook_name = wb_name.empty() ? "api-run" : wb_name;
      cfg.workbook_capital =
          Capital::from_paise(json_int_field(req.body, "capital_paise").value_or(avail));
      cfg.tickers = json_string_array(req.body, "tickers");
      if (cfg.tickers.empty()) {
        cfg.tickers = {"RELIANCE"};
      }
      cfg.strategies = json_string_array(req.body, "strategies");
      if (cfg.strategies.empty()) {
        cfg.strategies = {"ema_crossover"};
      }
      cfg.router = json_string_field(req.body, "router").value_or("default_router");
      cfg.from = Timestamp::from_nanos(
          json_int_field(req.body, "from_ns").value_or(1'788'921'000'000'000'000LL));
      cfg.to = Timestamp::from_nanos(
          json_int_field(req.body, "to_ns").value_or(1'789'064'940'000'000'000LL));
      cfg.trade_from = Timestamp::from_nanos(
          json_int_field(req.body, "trade_from_ns").value_or(1'789'065'000'000'000'000LL));
      cfg.trade_to = Timestamp::from_nanos(
          json_int_field(req.body, "trade_to_ns").value_or(1'789'151'340'000'000'000LL));

      for (const auto& t : cfg.tickers) {
        symbols_.intern({.ticker = t}, {});
      }

      WorkbookManager local_books;
      RunManager mgr;
      const auto result = mgr.execute(cfg, data_, strategies_, local_books, symbols_, &activity_);
      const auto wb_val = uuid_low(result.workbook_id);
      const auto runs = activity_.list_runs(wb_val);
      const auto run_id = runs.empty() ? 0 : runs.back().id;

      res.status = 201;
      res.body = "{\"run_id\":" + std::to_string(run_id) + ",\"workbook_id\":" +
                 std::to_string(wb_val) + ",\"selected\":" + std::to_string(result.selected) +
                 ",\"skipped\":" + std::to_string(result.skipped) +
                 ",\"fills\":" + std::to_string(result.fills) +
                 ",\"returned_paise\":" + std::to_string(result.returned.paise()) +
                 ",\"signals\":" + std::to_string(result.signals.size()) +
                 ",\"rejections\":" + std::to_string(result.rejections.size()) + "}";
      return res;
    }

    const std::regex re_runs(R"(/workbooks/(\d+)/runs)");
    if (req.method == "GET" && std::regex_match(req.path, m, re_runs)) {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto wid = std::stoll(m[1].str());
      const auto runs = activity_.list_runs(wid);
      std::ostringstream oss;
      oss << '[';
      for (std::size_t i = 0; i < runs.size(); ++i) {
        if (i) {
          oss << ',';
        }
        oss << "{\"id\":" << runs[i].id << ",\"router\":\"" << json_escape(runs[i].router)
            << "\",\"fills\":" << runs[i].fills << ",\"selected\":" << runs[i].selected
            << ",\"returned_paise\":" << runs[i].returned_paise << '}';
      }
      oss << ']';
      res.body = oss.str();
      return res;
    }

    const std::regex re_fills(R"(/workbooks/(\d+)/fills)");
    if (req.method == "GET" && std::regex_match(req.path, m, re_fills)) {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto wid = std::stoll(m[1].str());
      const auto runs = activity_.list_runs(wid);
      std::ostringstream oss;
      oss << '[';
      bool first = true;
      for (const auto& run : runs) {
        for (const auto& f : activity_.list_fills(run.id)) {
          if (!first) {
            oss << ',';
          }
          first = false;
          oss << "{\"ticker\":\"" << json_escape(f.ticker) << "\",\"side\":\"" << f.side
              << "\",\"qty\":" << f.qty << ",\"price_paise\":" << f.price_paise
              << ",\"timestamp_ns\":" << f.timestamp_ns << '}';
        }
      }
      oss << ']';
      res.body = oss.str();
      return res;
    }

    const std::regex re_portfolio(R"(/workbooks/(\d+)/portfolio)");
    const std::regex re_ws_portfolio(R"(/ws/workbooks/(\d+)/portfolio)");
    if (req.method == "GET" &&
        (std::regex_match(req.path, m, re_portfolio) ||
         std::regex_match(req.path, m, re_ws_portfolio))) {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto wid = std::stoll(m[1].str());
      sqlite3_stmt* st = nullptr;
      sqlite3_prepare_v2(db_.handle(),
                         "SELECT main_capital_paise, available_paise FROM workbooks "
                         "WHERE id=? AND deleted_at IS NULL",
                         -1, &st, nullptr);
      sqlite3_bind_int64(st, 1, wid);
      if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        res.status = 404;
        res.body = R"({"error":"workbook not found"})";
        return res;
      }
      res.body = "{\"workbook_id\":" + std::to_string(wid) + ",\"main_capital_paise\":" +
                 std::to_string(sqlite3_column_int64(st, 0)) + ",\"available_paise\":" +
                 std::to_string(sqlite3_column_int64(st, 1)) + "}";
      sqlite3_finalize(st);
      return res;
    }

    const std::regex re_containers(R"(/workbooks/(\d+)/containers)");
    const std::regex re_ws_containers(R"(/ws/workbooks/(\d+)/containers)");
    if (req.method == "GET" &&
        (std::regex_match(req.path, m, re_containers) ||
         std::regex_match(req.path, m, re_ws_containers))) {
      const auto claims = require_user(req, res);
      if (!claims) {
        return res;
      }
      const auto wid = std::stoll(m[1].str());
      const auto runs = activity_.list_runs(wid);
      std::ostringstream oss;
      oss << '[';
      bool first = true;
      for (const auto& run : runs) {
        for (const auto& c : activity_.list_containers(run.id)) {
          if (!first) {
            oss << ',';
          }
          first = false;
          oss << "{\"id\":" << c.id << ",\"ticker\":\"" << json_escape(c.ticker)
              << "\",\"strategy\":\"" << json_escape(c.strategy_name) << "\",\"mode\":\""
              << json_escape(c.mode) << "\",\"fills\":" << c.fills
              << ",\"realized_paise\":" << c.realized_paise << '}';
        }
      }
      oss << ']';
      res.body = oss.str();
      return res;
    }

    res.status = 404;
    res.body = R"({"error":"not found"})";
    return res;
  };

  while (running_) {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    const int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client), &len);
    if (client_fd < 0) {
      if (!running_) {
        break;
      }
      continue;
    }
    HttpRequest req;
    if (read_request(client_fd, req)) {
      const auto t0 = std::chrono::steady_clock::now();
      auto res = handle(req);
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
      spdlog::info("http {} {} -> {} ({} ms, body {} bytes)", req.method, req.path, res.status, ms,
                   req.body.size());
      if (res.status >= 400) {
        spdlog::warn("http error body={}", res.body);
      }
      write_response(client_fd, res);
    } else {
      spdlog::warn("http: failed to parse request");
    }
    ::close(client_fd);
  }

  running_ = false;
}

}  // namespace algocraft
