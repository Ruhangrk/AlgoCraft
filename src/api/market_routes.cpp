#include "algocraft/api/market_routes.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/market_data/cached_provider.hpp"
#include "algocraft/market_data/upstox_provider.hpp"

namespace algocraft::api {
namespace {

constexpr int kInstrumentSearchCap = 50;

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

crow::json::wvalue instrument_json(const InstrumentRow& row) {
  crow::json::wvalue root;
  root["ticker"] = row.ticker;
  root["name"] = row.name;
  root["isin"] = row.isin;
  root["exchange"] = row.exchange;
  root["segment"] = row.segment;
  root["lot_size"] = row.lot_size;
  root["tick_size_paise"] = row.tick_size_paise;
  root["instrument_key"] = row.instrument_key;
  root["active"] = row.active;
  return root;
}

int parse_limit(const crow::request& req, int fallback, int cap) {
  const auto* raw = req.url_params.get("limit");
  if (raw == nullptr || *raw == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const long v = std::strtol(raw, &end, 10);
  if (end == raw || v <= 0) {
    return fallback;
  }
  return static_cast<int>(v > cap ? cap : v);
}

std::optional<BarResolution> parse_chart_resolution(std::string_view code) {
  const auto parsed = bar_resolution_from_code(code);
  if (!parsed || !is_chart_resolution(*parsed)) {
    return std::nullopt;
  }
  return parsed;
}

void bind_upstox_instrument_key(DataSourceRegistry& data, std::string_view ticker,
                                std::string_view instrument_key) {
  if (instrument_key.empty()) {
    return;
  }
  auto* cached = dynamic_cast<CachedProvider*>(&data.active_provider());
  if (cached == nullptr) {
    return;
  }
  auto* upstox = dynamic_cast<UpstoxProvider*>(&cached->vendor());
  if (upstox == nullptr) {
    return;
  }
  upstox->upstox_loader().set_instrument_key(ticker, std::string{instrument_key});
}

crow::json::wvalue candles_json(const std::vector<BarEvent>& bars) {
  crow::json::wvalue root = crow::json::wvalue::list();
  for (std::size_t i = 0; i < bars.size(); ++i) {
    root[i]["timestamp_ns"] = bars[i].timestamp.nanos();
    root[i]["open_paise"] = bars[i].open.paise();
    root[i]["high_paise"] = bars[i].high.paise();
    root[i]["low_paise"] = bars[i].low.paise();
    root[i]["close_paise"] = bars[i].close.paise();
    root[i]["volume"] = bars[i].volume.shares();
  }
  return root;
}

}  // namespace

void register_market_routes(App& app, MarketRouteDeps deps) {
  auto* auth = &deps.auth;
  auto* strategies = &deps.strategies;
  auto* data = &deps.data;
  auto* symbols = &deps.symbols;
  auto* fetch = deps.fetch;
  auto* instruments = deps.instruments;

  CROW_ROUTE(app, "/strategies")
      .methods(crow::HTTPMethod::GET)([strategies] {
        crow::json::wvalue root;
        root = strategies->names();
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/routing-algos")
      .methods(crow::HTTPMethod::GET)([] { return json_response(200, R"(["default_router"])"); });

  CROW_ROUTE(app, "/instruments")
      .methods(crow::HTTPMethod::GET)([auth, instruments](const crow::request& req) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (instruments == nullptr) {
          return json_error(503, "instruments catalog not configured");
        }
        const auto* q_raw = req.url_params.get("q");
        const std::string q = q_raw ? q_raw : "";
        const int limit = parse_limit(req, 20, kInstrumentSearchCap);
        crow::json::wvalue root = crow::json::wvalue::list();
        if (q.empty()) {
          return json_ok(std::move(root));
        }
        const auto rows = instruments->search(q, limit);
        for (std::size_t i = 0; i < rows.size(); ++i) {
          root[i] = instrument_json(rows[i]);
        }
        return json_ok(std::move(root));
      });

  // Register before /instruments/<string> so Crow matches the longer path.
  CROW_ROUTE(app, "/instruments/<string>/ohlcv")
      .methods(crow::HTTPMethod::GET)([auth, instruments, symbols, data, fetch](
                                          const crow::request& req, const std::string& ticker) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (instruments == nullptr || fetch == nullptr) {
          return json_error(503, "ohlcv not configured");
        }
        const auto row = instruments->find_by_ticker(ticker);
        if (!row || !row->active) {
          return json_error(404, "instrument not found");
        }

        const auto* res_raw = req.url_params.get("resolution");
        const std::string res_code = res_raw ? res_raw : "";
        const auto resolution = parse_chart_resolution(res_code);
        if (!resolution) {
          return json_error(400, "resolution must be 1d, 1w, or 1M");
        }

        const auto* from_raw = req.url_params.get("from");
        const auto* to_raw = req.url_params.get("to");
        if (from_raw == nullptr || to_raw == nullptr || *from_raw == '\0' || *to_raw == '\0') {
          return json_error(400, "from and to are required (YYYY-MM-DD)");
        }
        Timestamp from_ts{};
        Timestamp to_ts{};
        try {
          from_ts = parse_ymd_ist_midnight(from_raw);
          to_ts = parse_ymd_ist_eod(to_raw);
        } catch (...) {
          return json_error(400, "from/to must be YYYY-MM-DD");
        }
        if (from_ts > to_ts) {
          return json_error(400, "from must be <= to");
        }

        symbols->intern({.ticker = ticker}, {});
        bind_upstox_instrument_key(*data, ticker, row->instrument_key);

        const auto symbol_id = symbols->find(ticker);
        if (!symbol_id) {
          return json_error(500, "symbol intern failed");
        }

        const auto before = fetch->vendor_fetches();
        std::vector<BarEvent> bars;
        try {
          bars = fetch->load_bars(*symbol_id, from_ts, to_ts, *resolution);
        } catch (const std::exception& e) {
          return json_error(502, e.what());
        }

        crow::json::wvalue root;
        root["ticker"] = ticker;
        root["resolution"] = res_code;
        root["from"] = from_raw;
        root["to"] = to_raw;
        root["vendor_fetches"] = static_cast<std::int64_t>(fetch->vendor_fetches() - before);
        root["candles"] = candles_json(bars);
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/instruments/<string>")
      .methods(crow::HTTPMethod::GET)([auth, instruments](const crow::request& req,
                                                          const std::string& ticker) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (instruments == nullptr) {
          return json_error(503, "instruments catalog not configured");
        }
        const auto row = instruments->find_by_ticker(ticker);
        if (!row || !row->active) {
          return json_error(404, "instrument not found");
        }
        return json_ok(instrument_json(*row));
      });

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
