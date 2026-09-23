#include "algocraft/market_data/upstox_provider.hpp"

#include "algocraft/market_data/upstox_live_feed.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "algocraft/domain/bar_event.hpp"
#include "algocraft/domain/price.hpp"
#include "algocraft/domain/quantity.hpp"

namespace algocraft {
namespace {

std::mutex g_rate_mu;
std::chrono::steady_clock::time_point g_last_call{};

void throttle(int min_interval_ms) {
  if (min_interval_ms <= 0) {
    return;
  }
  std::lock_guard lock(g_rate_mu);
  const auto now = std::chrono::steady_clock::now();
  if (g_last_call.time_since_epoch().count() != 0) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_call).count();
    if (elapsed < min_interval_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(min_interval_ms - elapsed));
    }
  }
  g_last_call = std::chrono::steady_clock::now();
}

std::size_t write_body(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string url_encode(CURL* curl, std::string_view value) {
  char* encoded = curl_easy_escape(curl, value.data(), static_cast<int>(value.size()));
  if (encoded == nullptr) {
    throw std::runtime_error("curl_easy_escape failed");
  }
  std::string out{encoded};
  curl_free(encoded);
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

// Parse "2026-09-11T09:15:00+05:30" → UTC nanos.
Timestamp parse_ist_iso(std::string_view s) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (std::sscanf(s.data(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 6) {
    throw std::runtime_error("bad candle timestamp");
  }
  using namespace std::chrono;
  // Timestamp is IST wall clock in the string; convert to UTC by subtracting +05:30.
  const auto utc = sys_days{year{y} / mo / d} + hours{h} + minutes{mi} + seconds{se} - hours{5} -
                   minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

std::int64_t rupees_to_paise(double px) {
  return static_cast<std::int64_t>(std::llround(px * 100.0));
}

std::string ymd(Timestamp ts) {
  using namespace std::chrono;
  // Convert UTC nanos → IST date for Upstox path params.
  const auto ist = sys_time<nanoseconds>{nanoseconds{ts.nanos()}} + hours{5} + minutes{30};
  const auto dp = floor<days>(ist);
  const year_month_day ymd{dp};
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
  return buf;
}

Timestamp ist_midnight(int year, unsigned month, unsigned day) {
  using namespace std::chrono;
  const auto utc = sys_days{std::chrono::year{year} / month / day} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

std::chrono::year_month_day ist_ymd(Timestamp ts) {
  using namespace std::chrono;
  const auto ist = sys_time<nanoseconds>{nanoseconds{ts.nanos()}} + hours{5} + minutes{30};
  return year_month_day{floor<days>(ist)};
}

Timestamp add_calendar_days_ist(Timestamp ts, int days_delta) {
  using namespace std::chrono;
  const auto ymd0 = ist_ymd(ts);
  const sys_days dp = sys_days{ymd0} + days{days_delta};
  const year_month_day ymd1{dp};
  return ist_midnight(static_cast<int>(ymd1.year()), static_cast<unsigned>(ymd1.month()),
                      static_cast<unsigned>(ymd1.day()));
}

}  // namespace

UpstoxHistoryUnit upstox_history_unit(BarResolution resolution) {
  switch (resolution) {
    case BarResolution::OneMin:
      return {.unit = "minutes", .interval = 1, .max_window_calendar_days = 28};
    case BarResolution::OneDay:
      // Official max ~1 decade; AlgoCraft chunk = 9 years.
      return {.unit = "days", .interval = 1, .max_window_calendar_days = 9 * 365};
    case BarResolution::OneWeek:
      // No short documented cap; still chunk generously (same as days).
      return {.unit = "weeks", .interval = 1, .max_window_calendar_days = 9 * 365};
    case BarResolution::OneMonth:
      return {.unit = "months", .interval = 1, .max_window_calendar_days = 9 * 365};
    default:
      throw std::runtime_error("upstox: unsupported resolution");
  }
}

std::vector<std::pair<Timestamp, Timestamp>> upstox_chunk_range(Timestamp from, Timestamp to,
                                                                int max_window_calendar_days) {
  std::vector<std::pair<Timestamp, Timestamp>> out;
  if (from.nanos() > to.nanos() || max_window_calendar_days <= 0) {
    return out;
  }
  Timestamp cursor = from;
  while (cursor.nanos() <= to.nanos()) {
    auto chunk_to = add_calendar_days_ist(cursor, max_window_calendar_days - 1);
    if (chunk_to.nanos() > to.nanos()) {
      chunk_to = to;
    }
    out.emplace_back(cursor, chunk_to);
    if (chunk_to.nanos() >= to.nanos()) {
      break;
    }
    cursor = add_calendar_days_ist(chunk_to, 1);
  }
  return out;
}

namespace {

std::vector<BarEvent> parse_candles(SymbolId symbol_id, BarResolution resolution,
                                    std::string_view body, Timestamp from, Timestamp to) {
  std::vector<BarEvent> out;
  const auto key = body.find("\"candles\"");
  if (key == std::string_view::npos) {
    return out;
  }
  const auto lb = body.find('[', key);
  if (lb == std::string_view::npos) {
    return out;
  }
  // Walk candle arrays: ["ts", o, h, l, c, v, oi]
  std::size_t i = lb + 1;
  while (i < body.size()) {
    while (i < body.size() && (body[i] == ' ' || body[i] == '\n' || body[i] == ',' || body[i] == '\r')) {
      ++i;
    }
    if (i >= body.size() || body[i] == ']') {
      break;
    }
    if (body[i] != '[') {
      ++i;
      continue;
    }
    ++i;
    while (i < body.size() && body[i] != '"') {
      ++i;
    }
    if (i >= body.size()) {
      break;
    }
    const auto q1 = i + 1;
    const auto q2 = body.find('"', q1);
    if (q2 == std::string_view::npos) {
      break;
    }
    const auto ts = parse_ist_iso(body.substr(q1, q2 - q1));
    i = q2 + 1;
    double o = 0, h = 0, l = 0, c = 0, v = 0;
    if (std::sscanf(body.data() + i, " , %lf , %lf , %lf , %lf , %lf", &o, &h, &l, &c, &v) < 5) {
      // tolerate no spaces
      if (std::sscanf(body.data() + i, ",%lf,%lf,%lf,%lf,%lf", &o, &h, &l, &c, &v) < 5) {
        const auto end = body.find(']', i);
        i = end == std::string_view::npos ? body.size() : end + 1;
        continue;
      }
    }
    const auto end = body.find(']', i);
    i = end == std::string_view::npos ? body.size() : end + 1;

    if (ts.nanos() < from.nanos() || ts.nanos() > to.nanos()) {
      continue;
    }
    BarEvent bar{};
    bar.symbol_id = symbol_id;
    bar.timestamp = ts;
    bar.resolution = resolution;
    bar.open = Price::from_paise(rupees_to_paise(o));
    bar.high = Price::from_paise(rupees_to_paise(h));
    bar.low = Price::from_paise(rupees_to_paise(l));
    bar.close = Price::from_paise(rupees_to_paise(c));
    bar.volume = Quantity::from_shares(static_cast<std::int64_t>(v));
    out.push_back(bar);
  }
  // Upstox returns newest-first; we want ascending.
  std::sort(out.begin(), out.end(), [](const BarEvent& a, const BarEvent& b) {
    return a.timestamp < b.timestamp;
  });
  return out;
}

std::string home_config_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr) {
    return {};
  }
  return std::string(home) + "/.config/upstox/config.json";
}

}  // namespace

UpstoxConfig UpstoxConfig::from_default_file() { return from_file(home_config_path()); }

UpstoxConfig UpstoxConfig::from_file(std::string_view path) {
  UpstoxConfig cfg;
  if (path.empty()) {
    return cfg;
  }
  std::ifstream in{std::string{path}};
  if (!in) {
    return cfg;
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  const auto body = oss.str();
  if (auto t = json_string_field(body, "upstox_access_token")) {
    cfg.access_token = *t;
  } else if (auto t = json_string_field(body, "access_token")) {
    cfg.access_token = *t;
  }
  return cfg;
}

UpstoxHistoricalLoader::UpstoxHistoricalLoader(UpstoxConfig config, const SymbolTable* symbols)
    : config_{std::move(config)}, symbols_{symbols} {}

void UpstoxHistoricalLoader::set_instrument_key(std::string_view ticker, std::string instrument_key) {
  ticker_to_key_[std::string{ticker}] = std::move(instrument_key);
}

std::string UpstoxHistoricalLoader::instrument_key_for(SymbolId id) const {
  if (symbols_ == nullptr || !symbols_->contains(id)) {
    throw std::runtime_error("upstox: unknown symbol id");
  }
  const auto& ticker = symbols_->symbol(id).ticker;
  const auto it = ticker_to_key_.find(ticker);
  if (it == ticker_to_key_.end()) {
    throw std::runtime_error("upstox: no instrument_key for " + ticker);
  }
  return it->second;
}

std::string UpstoxHistoricalLoader::http_get(std::string_view url) const {
  throttle(config_.min_interval_ms);
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  std::string body;
  const std::string auth = "Authorization: Bearer " + config_.access_token;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, auth.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, std::string{url}.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "algocraft/0.1");

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  ++http_calls_;

  if (rc != CURLE_OK) {
    throw std::runtime_error(std::string("upstox http: ") + curl_easy_strerror(rc));
  }
  if (status < 200 || status >= 300) {
    // Do not include response body — may echo auth errors with sensitive context.
    throw std::runtime_error("upstox http status " + std::to_string(status));
  }
  return body;
}

std::string UpstoxHistoricalLoader::build_history_url(std::string_view encoded_key,
                                                      BarResolution resolution, Timestamp from,
                                                      Timestamp to) const {
  const auto unit = upstox_history_unit(resolution);
  // Path order: to_date then from_date (Upstox v3).
  return config_.history_base_url + "/historical-candle/" + std::string{encoded_key} + "/" +
         std::string{unit.unit} + "/" + std::to_string(unit.interval) + "/" + ymd(to) + "/" +
         ymd(from);
}

std::vector<BarEvent> UpstoxHistoricalLoader::load_bars(SymbolId symbol_id, Timestamp from,
                                                        Timestamp to, BarResolution resolution) {
  if (!config_.ok()) {
    throw std::runtime_error("upstox: missing access token");
  }
  if (from.nanos() > to.nanos()) {
    return {};
  }
  const auto unit = upstox_history_unit(resolution);  // throws if unsupported
  const auto key = instrument_key_for(symbol_id);
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  const auto encoded = url_encode(curl, key);
  curl_easy_cleanup(curl);

  std::vector<BarEvent> all;
  for (const auto& [chunk_from, chunk_to] :
       upstox_chunk_range(from, to, unit.max_window_calendar_days)) {
    const auto url = build_history_url(encoded, resolution, chunk_from, chunk_to);
    const auto body = http_get(url);
    auto part = parse_candles(symbol_id, resolution, body, from, to);
    all.insert(all.end(), part.begin(), part.end());
  }
  std::sort(all.begin(), all.end(), [](const BarEvent& a, const BarEvent& b) {
    return a.timestamp < b.timestamp;
  });
  all.erase(std::unique(all.begin(), all.end(),
                        [](const BarEvent& a, const BarEvent& b) {
                          return a.timestamp == b.timestamp;
                        }),
            all.end());
  return all;
}

UpstoxProvider::UpstoxProvider(UpstoxConfig config, const SymbolTable* symbols)
    : config_(config), symbols_(symbols), loader_(std::move(config), symbols) {
  register_default_nse_eq(loader_);
}

UpstoxProvider::~UpstoxProvider() = default;

MarketDataFeed* UpstoxProvider::live_feed() {
  if (!live_) {
    live_ = std::make_unique<UpstoxLiveFeed>(config_, symbols_, loader_.instrument_keys());
  }
  return live_.get();
}

DataProviderCapabilities UpstoxProvider::capabilities() const {
  DataProviderCapabilities caps;
  caps.supported_resolutions = {BarResolution::OneMin, BarResolution::OneDay,
                                BarResolution::OneWeek, BarResolution::OneMonth};
  // Tightest per-request window (1m). Chart TFs use larger chunks via upstox_history_unit.
  caps.max_historical_lookback_days = 28;
  caps.rate_limit_per_second = 0;  // ~30/min enforced in loader
  return caps;
}

void UpstoxProvider::register_default_nse_eq(UpstoxHistoricalLoader& loader) {
  // DefaultRouter universe — trading_symbol → NSE_EQ|ISIN
  const std::pair<const char*, const char*> rows[] = {
      {"RELIANCE", "NSE_EQ|INE002A01018"},   {"INFY", "NSE_EQ|INE009A01021"},
      {"TCS", "NSE_EQ|INE467B01029"},        {"HDFCBANK", "NSE_EQ|INE040A01034"},
      {"ICICIBANK", "NSE_EQ|INE090A01021"},  {"SBIN", "NSE_EQ|INE062A01020"},
      {"BHARTIARTL", "NSE_EQ|INE397D01024"}, {"ITC", "NSE_EQ|INE154A01025"},
      {"LT", "NSE_EQ|INE018A01030"},         {"HINDUNILVR", "NSE_EQ|INE030A01027"},
  };
  for (const auto& [ticker, key] : rows) {
    loader.set_instrument_key(ticker, key);
  }
}

}  // namespace algocraft
