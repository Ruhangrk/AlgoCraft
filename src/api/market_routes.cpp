#include "algocraft/api/market_routes.hpp"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/timestamp.hpp"

namespace algocraft::api {
namespace {

Timestamp parse_ymd_ist_midnight(std::string_view ymd) {
  int y = 0;
  int m = 0;
  int d = 0;
  if (std::sscanf(ymd.data(), "%d-%d-%d", &y, &m, &d) != 3) {
    throw std::runtime_error("bad date");
  }
  using namespace std::chrono;
  const auto utc = sys_days{year{y} / m / d} + hours{0} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

Timestamp parse_ymd_ist_eod(std::string_view ymd) {
  int y = 0;
  int m = 0;
  int d = 0;
  if (std::sscanf(ymd.data(), "%d-%d-%d", &y, &m, &d) != 3) {
    throw std::runtime_error("bad date");
  }
  using namespace std::chrono;
  const auto utc =
      sys_days{year{y} / m / d} + hours{23} + minutes{59} - hours{5} - minutes{30};
  return Timestamp::from_nanos(duration_cast<nanoseconds>(utc.time_since_epoch()).count());
}

}  // namespace

void register_market_routes(App& app, MarketRouteDeps deps) {
  auto* auth = &deps.auth;
  auto* strategies = &deps.strategies;
  auto* data = &deps.data;
  auto* symbols = &deps.symbols;
  auto* fetch = deps.fetch;

  CROW_ROUTE(app, "/strategies")
      .methods(crow::HTTPMethod::GET)([strategies] {
        crow::json::wvalue root;
        root = strategies->names();
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/routing-algos")
      .methods(crow::HTTPMethod::GET)([] { return json_response(200, R"(["default_router"])"); });

  CROW_ROUTE(app, "/market-data/ensure")
      .methods(crow::HTTPMethod::POST)([auth, data, symbols, fetch](const crow::request& req) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (fetch == nullptr) {
          return json_error(503, "market-data fetch not configured");
        }
        const auto body = body_or_empty(req);
        auto tickers = json_string_array(body, "tickers");
        if (tickers.empty()) {
          tickers = {"RELIANCE", "INFY", "TCS", "HDFCBANK", "ICICIBANK"};
        }
        Timestamp from_ts{};
        Timestamp to_ts{};
        try {
          from_ts = parse_ymd_ist_midnight(json_string_or(body, "from", "2026-08-18"));
          to_ts = parse_ymd_ist_eod(json_string_or(body, "to", "2026-09-11"));
        } catch (...) {
          return json_error(400, "from/to must be YYYY-MM-DD");
        }

        const auto before = fetch->vendor_fetches();
        crow::json::wvalue root;
        root["results"] = crow::json::wvalue::list();
        for (std::size_t i = 0; i < tickers.size(); ++i) {
          symbols->intern({.ticker = tickers[i]}, {});
          std::string fetch_err;
          try {
            fetch->ensure_data_available(tickers[i], from_ts, to_ts, BarResolution::OneMin);
          } catch (const std::exception& e) {
            fetch_err = e.what();
          }
          root["results"][i]["ticker"] = tickers[i];
          root["results"][i]["ok"] = fetch_err.empty();
          if (!fetch_err.empty()) {
            root["results"][i]["error"] = fetch_err;
          }
        }
        root["source"] = std::string{data->active_provider().name()};
        root["vendor_fetches"] = static_cast<std::int64_t>(fetch->vendor_fetches() - before);
        return json_ok(std::move(root));
      });
}

}  // namespace algocraft::api
