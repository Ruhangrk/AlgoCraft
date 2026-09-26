#include "algocraft/api/workbook_routes.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "algocraft/backtest/backtest_service.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/session_date.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/engine/live_run_service.hpp"
#include "algocraft/engine/run_manager.hpp"
#include "algocraft/routing/routing_algo_registry.hpp"

namespace algocraft::api {
namespace {

struct WsAuth {
  AuthTokenClaims claims;
  std::int64_t workbook_id{};
};

void release_ws_auth(crow::websocket::connection& conn) {
  auto* ctx = static_cast<WsAuth*>(conn.userdata());
  // Crow may invoke onclose more than once (check_destroy without setting its
  // close-handler guard). Null before delete so a second call is a no-op.
  conn.userdata(nullptr);
  delete ctx;
}

std::int64_t uuid_low(const Uuid& id) {
  std::uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val = (val << 8) | id.bytes[static_cast<std::size_t>(8 + i)];
  }
  return static_cast<std::int64_t>(val);
}

std::string query_string(const crow::request& req, const char* key) {
  const auto* raw = req.url_params.get(key);
  return (raw == nullptr) ? std::string{} : std::string{raw};
}

std::int64_t query_int64(const crow::request& req, const char* key, std::int64_t fallback) {
  const auto* raw = req.url_params.get(key);
  if (raw == nullptr || *raw == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const auto v = std::strtoll(raw, &end, 10);
  if (end == raw) {
    return fallback;
  }
  return v;
}

int query_limit(const crow::request& req, int fallback, int cap) {
  const auto v = static_cast<int>(query_int64(req, "limit", fallback));
  if (v <= 0) {
    return fallback;
  }
  return v > cap ? cap : v;
}

ActivityRepository::ListFilter run_list_filter(const crow::request& req) {
  ActivityRepository::ListFilter f;
  f.from_date = query_string(req, "from");
  f.to_date = query_string(req, "to");
  f.limit = query_limit(req, 0, 500);
  f.cursor = query_int64(req, "cursor", 0);
  return f;
}

BacktestListFilter backtest_list_filter(const crow::request& req) {
  BacktestListFilter f;
  f.from_date = query_string(req, "from");
  f.to_date = query_string(req, "to");
  f.limit = query_limit(req, 0, 500);
  f.cursor = query_int64(req, "cursor", 0);
  return f;
}

crow::json::wvalue workbooks_json(const std::vector<WorkbookRepository::Row>& rows) {
  crow::json::wvalue root = crow::json::wvalue::list();
  for (std::size_t i = 0; i < rows.size(); ++i) {
    root[i]["id"] = rows[i].id;
    root[i]["name"] = rows[i].name;
    root[i]["main_capital_paise"] = rows[i].main_capital_paise;
    root[i]["available_paise"] = rows[i].available_paise;
  }
  return root;
}

crow::json::wvalue portfolio_json(std::int64_t wid, const WorkbookRepository::Row& row) {
  crow::json::wvalue root;
  root["workbook_id"] = wid;
  root["main_capital_paise"] = row.main_capital_paise;
  root["available_paise"] = row.available_paise;
  return root;
}

crow::json::wvalue runs_json(const std::vector<ActivityRepository::RunRow>& runs) {
  crow::json::wvalue root = crow::json::wvalue::list();
  for (std::size_t i = 0; i < runs.size(); ++i) {
    root[i]["id"] = runs[i].id;
    root[i]["router"] = runs[i].router;
    root[i]["fills"] = runs[i].fills;
    root[i]["selected"] = runs[i].selected;
    root[i]["returned_paise"] = runs[i].returned_paise;
    root[i]["created_at"] = runs[i].created_at;
  }
  return root;
}

crow::json::wvalue fills_json(ActivityRepository& activity, std::int64_t wid) {
  crow::json::wvalue root = crow::json::wvalue::list();
  std::size_t i = 0;
  for (const auto& run : activity.list_runs(wid)) {
    for (const auto& f : activity.list_fills(run.id)) {
      root[i]["ticker"] = f.ticker;
      root[i]["side"] = f.side;
      root[i]["qty"] = f.qty;
      root[i]["price_paise"] = f.price_paise;
      root[i]["timestamp_ns"] = f.timestamp_ns;
      ++i;
    }
  }
  return root;
}

crow::json::wvalue container_json(const ActivityRepository::ContainerRow& c) {
  crow::json::wvalue root;
  root["id"] = c.id;
  root["run_id"] = c.run_id;
  root["workbook_id"] = c.workbook_id;
  root["ticker"] = c.ticker;
  root["strategy"] = c.strategy_name;
  root["mode"] = c.mode;
  root["allocation_paise"] = c.allocation_paise;
  root["realized_paise"] = c.realized_paise;
  root["fills"] = c.fills;
  root["created_at"] = c.created_at;
  return root;
}

crow::json::wvalue containers_json(ActivityRepository& activity, std::int64_t wid) {
  crow::json::wvalue root = crow::json::wvalue::list();
  std::size_t i = 0;
  for (const auto& run : activity.list_runs(wid)) {
    for (const auto& c : activity.list_containers(run.id)) {
      root[i] = container_json(c);
      ++i;
    }
  }
  return root;
}

bool include_has(std::string_view include, std::string_view key) {
  if (include.empty() || include == "all") {
    return true;
  }
  std::size_t start = 0;
  while (start <= include.size()) {
    const auto comma = include.find(',', start);
    const auto part =
        include.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (part == key) {
      return true;
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return false;
}

// container_id == 0 → all events for the run. Otherwise filter rows that carry container_id.
// Routing decisions have no container_id; omitted when filtering.
crow::json::wvalue run_events_json(ActivityRepository& activity, std::int64_t rid,
                                   std::string_view include, std::int64_t container_id) {
  struct Item {
    std::int64_t timestamp_ns{};
    std::int64_t id{};
    std::string type;
    crow::json::wvalue body;
  };
  std::vector<Item> items;
  const bool filter = container_id > 0;

  if (include_has(include, "signal") || include_has(include, "signals")) {
    for (const auto& s : activity.list_signals(rid)) {
      if (filter && s.container_id != container_id) {
        continue;
      }
      crow::json::wvalue body;
      body["id"] = s.id;
      body["container_id"] = s.container_id;
      body["ticker"] = s.ticker;
      body["strategy"] = s.strategy_name;
      body["intent_count"] = s.intent_count;
      body["indicators_json"] = s.indicators_json;
      body["timestamp_ns"] = s.timestamp_ns;
      items.push_back({s.timestamp_ns, s.id, "signal", std::move(body)});
    }
  }
  if (include_has(include, "rejection") || include_has(include, "rejections")) {
    for (const auto& r : activity.list_rejections(rid)) {
      if (filter && r.container_id != container_id) {
        continue;
      }
      crow::json::wvalue body;
      body["id"] = r.id;
      body["container_id"] = r.container_id;
      body["ticker"] = r.ticker;
      body["rule"] = r.rule_name;
      body["reason"] = r.reason;
      body["timestamp_ns"] = r.timestamp_ns;
      items.push_back({r.timestamp_ns, r.id, "rejection", std::move(body)});
    }
  }
  if (include_has(include, "fill") || include_has(include, "fills")) {
    for (const auto& f : activity.list_fills(rid)) {
      if (filter && f.container_id != container_id) {
        continue;
      }
      crow::json::wvalue body;
      body["id"] = f.id;
      body["container_id"] = f.container_id;
      body["ticker"] = f.ticker;
      body["side"] = f.side;
      body["qty"] = f.qty;
      body["price_paise"] = f.price_paise;
      body["fees_paise"] = f.fees_paise;
      body["timestamp_ns"] = f.timestamp_ns;
      items.push_back({f.timestamp_ns, f.id, "fill", std::move(body)});
    }
  }
  if (!filter && (include_has(include, "routing") || include_has(include, "routing_decisions"))) {
    for (const auto& r : activity.list_routing(rid)) {
      crow::json::wvalue body;
      body["id"] = r.id;
      body["ticker"] = r.ticker;
      body["strategy"] = r.strategy_name;
      body["decision"] = r.decision;
      body["score_paise"] = r.score_paise;
      body["reason"] = r.reason;
      body["timestamp_ns"] = r.timestamp_ns;
      items.push_back({r.timestamp_ns, r.id, "routing", std::move(body)});
    }
  }
  if (include_has(include, "lifecycle") || include_has(include, "container_events")) {
    for (const auto& e : activity.list_lifecycle(rid)) {
      if (filter && e.container_id != container_id) {
        continue;
      }
      crow::json::wvalue body;
      body["id"] = e.id;
      body["container_id"] = e.container_id;
      body["ticker"] = e.ticker;
      body["event_type"] = e.event_type;
      body["detail"] = e.detail;
      body["timestamp_ns"] = e.timestamp_ns;
      items.push_back({e.timestamp_ns, e.id, "lifecycle", std::move(body)});
    }
  }

  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.timestamp_ns != b.timestamp_ns) {
      return a.timestamp_ns < b.timestamp_ns;
    }
    if (a.type != b.type) {
      return a.type < b.type;
    }
    return a.id < b.id;
  });

  crow::json::wvalue root = crow::json::wvalue::list();
  for (std::size_t i = 0; i < items.size(); ++i) {
    root[i]["type"] = items[i].type;
    root[i]["timestamp_ns"] = items[i].timestamp_ns;
    root[i]["id"] = items[i].id;
    root[i]["data"] = std::move(items[i].body);
  }
  return root;
}

crow::json::wvalue backtest_json(const BacktestRow& row) {
  crow::json::wvalue root;
  root["id"] = row.id;
  root["workbook_id"] = row.workbook_id;
  root["strategy"] = row.strategy_name;
  root["ticker"] = row.ticker;
  root["capital_paise"] = row.capital_paise;
  root["from_ns"] = row.from_ns;
  root["to_ns"] = row.to_ns;
  root["ending_equity_paise"] = row.ending_equity_paise;
  root["pnl_paise"] = row.pnl_paise;
  root["fees_paise"] = row.fees_paise;
  root["return_pct_bp"] = row.return_pct_bp;
  root["max_drawdown_paise"] = row.max_drawdown_paise;
  root["fills"] = row.fills;
  root["bars"] = row.bars;
  root["status"] = row.status;
  root["created_at"] = row.created_at;
  return root;
}

crow::json::wvalue backtests_json(const std::vector<BacktestRow>& rows) {
  crow::json::wvalue root = crow::json::wvalue::list();
  for (std::size_t i = 0; i < rows.size(); ++i) {
    root[i] = backtest_json(rows[i]);
  }
  return root;
}

crow::json::wvalue backtest_events_json(BacktestRepository& backtests, std::int64_t bid,
                                        std::string_view include) {
  struct Item {
    std::int64_t timestamp_ns{};
    std::int64_t id{};
    std::string type;
    crow::json::wvalue body;
  };
  std::vector<Item> items;

  if (include_has(include, "signal") || include_has(include, "signals")) {
    for (const auto& s : backtests.list_signals(bid)) {
      crow::json::wvalue body;
      body["id"] = s.id;
      body["container_id"] = 0;
      body["ticker"] = s.ticker;
      body["strategy"] = s.strategy_name;
      body["intent_count"] = s.intent_count;
      body["indicators_json"] = s.indicators_json;
      body["timestamp_ns"] = s.timestamp_ns;
      items.push_back({s.timestamp_ns, s.id, "signal", std::move(body)});
    }
  }
  if (include_has(include, "rejection") || include_has(include, "rejections")) {
    for (const auto& r : backtests.list_rejections(bid)) {
      crow::json::wvalue body;
      body["id"] = r.id;
      body["container_id"] = 0;
      body["ticker"] = r.ticker;
      body["rule"] = r.rule_name;
      body["reason"] = r.reason;
      body["timestamp_ns"] = r.timestamp_ns;
      items.push_back({r.timestamp_ns, r.id, "rejection", std::move(body)});
    }
  }
  if (include_has(include, "fill") || include_has(include, "fills")) {
    for (const auto& f : backtests.list_fills(bid)) {
      crow::json::wvalue body;
      body["id"] = f.id;
      body["container_id"] = 0;
      body["ticker"] = f.ticker;
      body["side"] = f.side;
      body["qty"] = f.qty;
      body["price_paise"] = f.price_paise;
      body["fees_paise"] = f.fees_paise;
      body["timestamp_ns"] = f.timestamp_ns;
      items.push_back({f.timestamp_ns, f.id, "fill", std::move(body)});
    }
  }

  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.timestamp_ns != b.timestamp_ns) {
      return a.timestamp_ns < b.timestamp_ns;
    }
    if (a.type != b.type) {
      return a.type < b.type;
    }
    return a.id < b.id;
  });

  crow::json::wvalue root = crow::json::wvalue::list();
  for (std::size_t i = 0; i < items.size(); ++i) {
    root[i]["type"] = items[i].type;
    root[i]["timestamp_ns"] = items[i].timestamp_ns;
    root[i]["id"] = items[i].id;
    root[i]["data"] = std::move(items[i].body);
  }
  return root;
}

std::optional<std::int64_t> parse_ws_workbook_id(std::string_view url, const char* suffix) {
  constexpr std::string_view kPrefix = "/ws/workbooks/";
  if (url.rfind(kPrefix, 0) != 0) {
    return std::nullopt;
  }
  const auto rest = url.substr(kPrefix.size());
  const auto slash = rest.find('/');
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  const auto id_part = rest.substr(0, slash);
  auto path_rest = rest.substr(slash + 1);
  const auto q = path_rest.find('?');
  if (q != std::string_view::npos) {
    path_rest = path_rest.substr(0, q);
  }
  if (path_rest != suffix) {
    return std::nullopt;
  }
  const std::string id_str{id_part};
  char* end = nullptr;
  const auto wid = std::strtoll(id_str.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return std::nullopt;
  }
  return wid;
}

}  // namespace

void register_workbook_routes(App& app, WorkbookRouteDeps deps) {
  auto* auth = &deps.auth;
  auto* workbooks = &deps.workbooks;
  auto* activity = &deps.activity;
  auto* books = &deps.books;
  auto* data = &deps.data;
  auto* strategies = &deps.strategies;
  auto* symbols = &deps.symbols;
  auto* fetch = deps.fetch;
  auto* backtests = deps.backtests;
  auto* instruments = deps.instruments;
  auto* live_runs = deps.live_runs;
  auto* status_hub = deps.status_hub;

  CROW_ROUTE(app, "/workbooks")
      .methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST)(
          [auth, workbooks, books](const crow::request& req) {
            auto gate = require_user(*auth, req);
            if (!gate) {
              return std::move(gate.error);
            }

            if (req.method == crow::HTTPMethod::GET) {
              return json_ok(workbooks_json(workbooks->list_for_user(gate.claims->user_id)));
            }

            const auto body = body_or_empty(req);
            const auto name = json_string_or(body, "name", "workbook");
            const auto capital = json_int_or(body, "capital_paise", 10'00'000'00);
            const auto uname = auth->find_user(gate.claims->user_id);
            const std::string username =
                uname ? uname->username : ("user_" + std::to_string(gate.claims->user_id));
            const std::string role = uname ? uname->role : "user";

            std::int64_t wb_val = 0;
            try {
              wb_val = workbooks->create(gate.claims->user_id, name, capital, username, role);
            } catch (const std::exception& e) {
              return json_error(500, e.what());
            }
            books->create(UserId::from_u64(static_cast<std::uint64_t>(gate.claims->user_id)), name,
                          Capital::from_paise(capital));

            crow::json::wvalue root;
            root["id"] = wb_val;
            root["name"] = name;
            root["capital_paise"] = capital;
            return json_ok(std::move(root), 201);
          });

  CROW_ROUTE(app, "/workbooks/<int>")
      .methods(crow::HTTPMethod::PATCH)([auth, workbooks](const crow::request& req,
                                                          std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto body = body_or_empty(req);
        const auto amount = json_int_or(body, "add_capital_paise", 0);
        if (amount <= 0) {
          return json_error(400, "add_capital_paise must be positive");
        }
        try {
          const auto updated = workbooks->add_capital(wid, amount);
          if (!updated) {
            return json_error(404, "workbook not found");
          }
          crow::json::wvalue root;
          root["id"] = updated->id;
          root["name"] = updated->name;
          root["main_capital_paise"] = updated->main_capital_paise;
          root["available_paise"] = updated->available_paise;
          root["added_paise"] = amount;
          return json_ok(std::move(root));
        } catch (const std::invalid_argument& e) {
          return json_error(400, e.what());
        } catch (const std::exception& e) {
          return json_error(500, e.what());
        }
      });

  CROW_ROUTE(app, "/workbooks/<int>/runs/start")
      .methods(crow::HTTPMethod::POST)([auth, workbooks, activity, data, strategies, symbols, books,
                                        live_runs](const crow::request& req, std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto row = workbooks->find(wid);
        if (!row) {
          return json_error(404, "workbook not found");
        }

        const auto body = body_or_empty(req);
        const auto wb_id = WorkbookId::from_u64(static_cast<std::uint64_t>(wid));
        RunConfig cfg{};
        cfg.user_id = UserId::from_u64(static_cast<std::uint64_t>(gate.claims->user_id));
        cfg.workbook_name = row->name.empty() ? "api-run" : row->name;
        cfg.workbook_capital =
            Capital::from_paise(json_int_or(body, "capital_paise", row->available_paise));
        cfg.existing_workbook_id = wb_id;
        cfg.router = json_string_or(body, "router", "default_router");

        // Tickers / strategies / eval_sessions come from the router, not the request body.
        RoutingAlgoRegistry routers;
        register_all_routers(routers);
        std::unique_ptr<RoutingAlgo> router_probe;
        try {
          router_probe = routers.create(cfg.router);
        } catch (const std::invalid_argument& e) {
          return json_error(400, e.what());
        }
        const auto defaults = router_probe->defaults();
        if (defaults.tickers.empty() || defaults.strategies.empty() || defaults.eval_sessions < 1) {
          return json_error(500, "router defaults incomplete");
        }
        cfg.tickers = defaults.tickers;
        cfg.strategies = defaults.strategies;

        const auto anchor_s = json_string_or(body, "anchor_date", "");
        if (!anchor_s.empty()) {
          try {
            const auto anchor = SessionDate::from_iso(anchor_s);
            std::optional<int> from_m;
            std::optional<int> to_m;
            const auto tf = json_string_or(body, "trade_from", "");
            const auto tt = json_string_or(body, "trade_to", "");
            if (!tf.empty() || !tt.empty()) {
              from_m = parse_hhmm(tf);
              to_m = parse_hhmm(tt);
              if (!from_m || !to_m) {
                return json_error(400, "trade_from/trade_to must be HH:MM");
              }
            }
            apply_run_windows(cfg,
                              resolve_anchor_run_windows(anchor, defaults.eval_sessions, from_m, to_m));
          } catch (const std::invalid_argument& e) {
            return json_error(400, e.what());
          }
        } else {
          cfg.from =
              Timestamp::from_nanos(json_int_or(body, "from_ns", 1'788'921'000'000'000'000LL));
          cfg.to = Timestamp::from_nanos(json_int_or(body, "to_ns", 1'789'064'940'000'000'000LL));
          cfg.trade_from =
              Timestamp::from_nanos(json_int_or(body, "trade_from_ns", 1'789'065'000'000'000'000LL));
          cfg.trade_to =
              Timestamp::from_nanos(json_int_or(body, "trade_to_ns", 1'789'151'340'000'000'000LL));
        }

        for (const auto& t : cfg.tickers) {
          symbols->intern({.ticker = t}, {});
        }

        auto fill_run_echo = [&](crow::json::wvalue& root) {
          root["router"] = cfg.router;
          crow::json::wvalue tickers = crow::json::wvalue::list();
          for (std::size_t i = 0; i < cfg.tickers.size(); ++i) {
            tickers[i] = cfg.tickers[i];
          }
          root["tickers"] = std::move(tickers);
          crow::json::wvalue strats = crow::json::wvalue::list();
          for (std::size_t i = 0; i < cfg.strategies.size(); ++i) {
            strats[i] = cfg.strategies[i];
          }
          root["strategies"] = std::move(strats);
          if (cfg.anchor_date) {
            root["anchor_date"] = cfg.anchor_date->iso();
            root["eval_sessions"] = cfg.eval_sessions;
            root["eval_from_ns"] = cfg.from.nanos();
            root["eval_to_ns"] = cfg.to.nanos();
            root["trade_from_ns"] = cfg.trade_from.nanos();
            root["trade_to_ns"] = cfg.trade_to.nanos();
          }
        };

        // Live path: anchor_date == IST today.
        if (cfg.anchor_date && is_live_anchor(*cfg.anchor_date)) {
          if (live_runs == nullptr) {
            return json_error(500, "live run service unavailable");
          }
          if (!books->get_workbook(wb_id)) {
            if (!books
                     ->adopt(wb_id, cfg.user_id, cfg.workbook_name,
                             Capital::from_paise(row->main_capital_paise),
                             Capital::from_paise(row->available_paise))
                     .ok) {
              return json_error(500, "failed to load workbook");
            }
          }
          const auto started = live_runs->start(cfg);
          if (!started.ok) {
            return json_error(409, started.error);
          }
          crow::json::wvalue root;
          root["workbook_id"] = wid;
          root["selected"] = started.result.selected;
          root["skipped"] = started.result.skipped;
          root["fills"] = started.result.fills;
          root["returned_paise"] = started.result.returned.paise();
          root["signals"] = static_cast<std::int64_t>(started.result.signals.size());
          root["rejections"] = static_cast<std::int64_t>(started.result.rejections.size());
          root["mode"] = "live";
          root["live"] = started.live_started;
          if (!started.live_started) {
            const auto runs = activity->list_runs(wid);
            root["run_id"] = runs.empty() ? 0 : runs.front().id;
          } else {
            root["run_id"] = 0;  // persisted on STOP / session end
          }
          fill_run_echo(root);
          return json_ok(std::move(root), 201);
        }

        WorkbookManager local_books;
        if (!local_books
                 .adopt(wb_id, cfg.user_id, cfg.workbook_name,
                        Capital::from_paise(row->main_capital_paise),
                        Capital::from_paise(row->available_paise))
                 .ok) {
          return json_error(500, "failed to load workbook");
        }
        RunManager mgr;
        const auto result =
            mgr.execute(cfg, *data, *strategies, local_books, *symbols, activity);
        if (uuid_low(result.workbook_id) != wid) {
          return json_error(500, "run workbook bind failed");
        }
        const auto runs = activity->list_runs(wid);
        crow::json::wvalue root;
        root["run_id"] = runs.empty() ? 0 : runs.front().id;
        root["workbook_id"] = wid;
        root["selected"] = result.selected;
        root["skipped"] = result.skipped;
        root["fills"] = result.fills;
        root["returned_paise"] = result.returned.paise();
        root["signals"] = static_cast<std::int64_t>(result.signals.size());
        root["rejections"] = static_cast<std::int64_t>(result.rejections.size());
        if (cfg.anchor_date) {
          root["mode"] = "hist_replay";
        }
        fill_run_echo(root);
        return json_ok(std::move(root), 201);
      });

  CROW_ROUTE(app, "/workbooks/<int>/runs/stop")
      .methods(crow::HTTPMethod::POST)([auth, workbooks, live_runs,
                                        activity](const crow::request& req, std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        if (live_runs == nullptr || !live_runs->is_running(wid)) {
          return json_error(404, "no active live run");
        }
        if (!live_runs->stop(wid)) {
          return json_error(404, "no active live run");
        }
        const auto runs = activity->list_runs(wid);
        crow::json::wvalue root;
        root["workbook_id"] = wid;
        root["stopped"] = true;
        root["run_id"] = runs.empty() ? 0 : runs.front().id;
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/workbooks/<int>/runs")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        return json_ok(runs_json(activity->list_runs(wid, run_list_filter(req))));
      });

  CROW_ROUTE(app, "/workbooks/<int>/runs/<int>")
      .methods(crow::HTTPMethod::Delete)([auth, workbooks, activity](const crow::request& req,
                                                                     std::int64_t wid,
                                                                     std::int64_t rid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        if (!activity->soft_delete_run(wid, rid)) {
          return json_error(404, "run not found");
        }
        crow::json::wvalue root;
        root["id"] = rid;
        root["deleted"] = true;
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/workbooks/<int>/runs/<int>/events")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid,
                                                                  std::int64_t rid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        if (!activity->find_run(wid, rid)) {
          return json_error(404, "run not found");
        }
        const auto include = query_string(req, "include");
        const auto container_id = query_int64(req, "container_id", 0);
        return json_ok(run_events_json(*activity, rid, include, container_id));
      });

  CROW_ROUTE(app, "/workbooks/<int>/history")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity, backtests](const crow::request& req,
                                                                            std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto type = query_string(req, "type");
        const auto want_runs = type.empty() || type == "all" || type == "run" || type == "runs";
        const auto want_bts =
            type.empty() || type == "all" || type == "backtest" || type == "backtests";
        if (!want_runs && !want_bts) {
          return json_error(400, "type must be run, backtest, or all");
        }

        const auto run_f = run_list_filter(req);
        const auto bt_f = backtest_list_filter(req);
        // When mixing types, apply limit after merge; skip per-table cursor.
        ActivityRepository::ListFilter run_q = run_f;
        BacktestListFilter bt_q = bt_f;
        if (want_runs && want_bts) {
          run_q.cursor = 0;
          bt_q.cursor = 0;
          run_q.limit = 0;
          bt_q.limit = 0;
        }

        struct Item {
          std::string type;
          std::string created_at;
          std::int64_t id{};
          crow::json::wvalue body;
        };
        std::vector<Item> items;
        if (want_runs) {
          for (const auto& r : activity->list_runs(wid, run_q)) {
            crow::json::wvalue body;
            body["id"] = r.id;
            body["router"] = r.router;
            body["fills"] = r.fills;
            body["selected"] = r.selected;
            body["returned_paise"] = r.returned_paise;
            body["created_at"] = r.created_at;
            items.push_back({"run", r.created_at, r.id, std::move(body)});
          }
        }
        if (want_bts) {
          if (backtests == nullptr) {
            return json_error(503, "backtests not configured");
          }
          for (const auto& b : backtests->list_for_workbook(wid, bt_q)) {
            auto body = backtest_json(b);
            body["return_pct"] = b.return_pct_bp / 100.0;
            items.push_back({"backtest", b.created_at, b.id, std::move(body)});
          }
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
          if (a.created_at != b.created_at) {
            return a.created_at > b.created_at;
          }
          if (a.type != b.type) {
            return a.type > b.type;
          }
          return a.id > b.id;
        });
        const int limit = query_limit(req, 0, 500);
        if (limit > 0 && static_cast<int>(items.size()) > limit) {
          items.resize(static_cast<std::size_t>(limit));
        }

        crow::json::wvalue root = crow::json::wvalue::list();
        for (std::size_t i = 0; i < items.size(); ++i) {
          root[i]["type"] = items[i].type;
          root[i]["id"] = items[i].id;
          root[i]["created_at"] = items[i].created_at;
          root[i]["item"] = std::move(items[i].body);
        }
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/workbooks/<int>/fills")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        return json_ok(fills_json(*activity, wid));
      });

  CROW_ROUTE(app, "/workbooks/<int>/portfolio")
      .methods(crow::HTTPMethod::GET)([auth, workbooks](const crow::request& req, std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto row = workbooks->find(wid);
        if (!row) {
          return json_error(404, "workbook not found");
        }
        return json_ok(portfolio_json(wid, *row));
      });

  // Register detail/events before the bare /containers list is fine (Crow matches by path).
  CROW_ROUTE(app, "/workbooks/<int>/containers/<int>/events")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid,
                                                                  std::int64_t cid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto row = activity->find_container(wid, cid);
        if (!row) {
          return json_error(404, "container not found");
        }
        const auto include = query_string(req, "include");
        return json_ok(run_events_json(*activity, row->run_id, include, cid));
      });

  CROW_ROUTE(app, "/workbooks/<int>/containers/<int>")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid,
                                                                  std::int64_t cid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        const auto row = activity->find_container(wid, cid);
        if (!row) {
          return json_error(404, "container not found");
        }
        return json_ok(container_json(*row));
      });

  CROW_ROUTE(app, "/workbooks/<int>/containers")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        return json_ok(containers_json(*activity, wid));
      });

  CROW_ROUTE(app, "/workbooks/<int>/backtests/start")
      .methods(crow::HTTPMethod::POST)([auth, workbooks, data, strategies, symbols, fetch,
                                        backtests, instruments](const crow::request& req,
                                                                std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        if (fetch == nullptr || backtests == nullptr) {
          return json_error(503, "backtests not configured");
        }
        const auto body = body_or_empty(req);
        ManualBacktestRequest bt{};
        bt.workbook_id = wid;
        bt.ticker = json_string_or(body, "ticker", "RELIANCE");
        bt.strategy_name = json_string_or(body, "strategy", "ema_crossover");
        const auto wb = workbooks->find(wid);
        if (!wb) {
          return json_error(404, "workbook not found");
        }
        bt.capital = Capital::from_paise(json_int_or(body, "capital_paise", 1'00'000'00));
        // capital_paise is simulated starting capital only — not deducted from workbook.
        bt.from = Timestamp::from_nanos(json_int_or(body, "from_ns", 0));
        bt.to = Timestamp::from_nanos(json_int_or(body, "to_ns", 0));
        if (bt.from.nanos() == 0 || bt.to.nanos() == 0) {
          return json_error(400, "from_ns and to_ns are required");
        }
        bt.strategy.order_qty =
            Quantity::from_shares(json_int_or(body, "order_qty", 1));
        bt.strategy.alloc_paise = bt.capital.paise();
        bt.strategy.ema_fast = static_cast<int>(json_int_or(body, "ema_fast", 9));
        bt.strategy.ema_slow = static_cast<int>(json_int_or(body, "ema_slow", 21));

        try {
          BacktestService svc(*workbooks, *backtests, *data, *fetch, *strategies, *symbols,
                              instruments);
          const auto outcome = svc.run(bt);
          auto root = backtest_json(outcome.row);
          root["return_pct"] = outcome.row.return_pct_bp / 100.0;
          return json_ok(std::move(root), 201);
        } catch (const std::invalid_argument& e) {
          return json_error(400, e.what());
        } catch (const std::exception& e) {
          return json_error(500, e.what());
        }
      });

  CROW_ROUTE(app, "/workbooks/<int>/backtests")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, backtests](const crow::request& req,
                                                                   std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        if (backtests == nullptr) {
          return json_error(503, "backtests not configured");
        }
        return json_ok(backtests_json(backtests->list_for_workbook(wid, backtest_list_filter(req))));
      });

  CROW_ROUTE(app, "/workbooks/<int>/backtests/<int>/events")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, backtests](const crow::request& req,
                                                                   std::int64_t wid,
                                                                   std::int64_t bid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        if (backtests == nullptr) {
          return json_error(503, "backtests not configured");
        }
        if (!backtests->find(wid, bid)) {
          return json_error(404, "backtest not found");
        }
        const auto include = req.url_params.get("include")
                                 ? std::string{req.url_params.get("include")}
                                 : std::string{"all"};
        return json_ok(backtest_events_json(*backtests, bid, include));
      });

  CROW_ROUTE(app, "/workbooks/<int>/backtests/<int>")
      .methods(crow::HTTPMethod::GET, crow::HTTPMethod::Delete)(
          [auth, workbooks, backtests](const crow::request& req, std::int64_t wid,
                                       std::int64_t bid) {
            auto gate = require_workbook(*auth, *workbooks, req, wid);
            if (!gate) {
              return std::move(gate.error);
            }
            if (backtests == nullptr) {
              return json_error(503, "backtests not configured");
            }
            if (req.method == crow::HTTPMethod::Delete) {
              if (!backtests->soft_delete(wid, bid)) {
                return json_error(404, "backtest not found");
              }
              crow::json::wvalue root;
              root["id"] = bid;
              root["deleted"] = true;
              return json_ok(std::move(root));
            }
            const auto row = backtests->find(wid, bid);
            if (!row) {
              return json_error(404, "backtest not found");
            }
            auto root = backtest_json(*row);
            root["return_pct"] = row->return_pct_bp / 100.0;
            return json_ok(std::move(root));
          });

  auto ws_accept = [auth, workbooks](const crow::request& req, void** userdata, const char* suffix) {
    auto gate = require_user(*auth, req);
    if (!gate) {
      return false;
    }
    const auto wid = parse_ws_workbook_id(req.url, suffix);
    if (!wid ||
        !workbooks->can_access(*wid, gate.claims->user_id, gate.claims->role == "admin")) {
      return false;
    }
    *userdata = new WsAuth{std::move(*gate.claims), *wid};
    return true;
  };

  // Use <int>, not <path>: Crow's <path> greedily consumes the rest of the URL, so
  // "/ws/workbooks/<path>/portfolio" never matches "/ws/workbooks/13/portfolio".
  CROW_WEBSOCKET_ROUTE(app, "/ws/workbooks/<int>/portfolio")
      .onaccept([ws_accept](const crow::request& req, void** userdata) {
        return ws_accept(req, userdata, "portfolio");
      })
      .onopen([workbooks, status_hub](crow::websocket::connection& conn) {
        auto* ctx = static_cast<WsAuth*>(conn.userdata());
        if (ctx == nullptr) {
          return;
        }
        if (status_hub != nullptr) {
          status_hub->register_conn(ctx->workbook_id, StatusWsHub::Channel::Portfolio, &conn);
        }
        if (const auto row = workbooks->find(ctx->workbook_id)) {
          conn.send_text(portfolio_json(ctx->workbook_id, *row).dump());
        }
      })
      .onclose([status_hub](crow::websocket::connection& conn, const std::string&, std::uint16_t) {
        if (status_hub != nullptr) {
          status_hub->unregister_conn(&conn);
        }
        release_ws_auth(conn);
      });

  CROW_WEBSOCKET_ROUTE(app, "/ws/workbooks/<int>/containers")
      .onaccept([ws_accept](const crow::request& req, void** userdata) {
        return ws_accept(req, userdata, "containers");
      })
      .onopen([activity, status_hub](crow::websocket::connection& conn) {
        auto* ctx = static_cast<WsAuth*>(conn.userdata());
        if (ctx == nullptr) {
          return;
        }
        if (status_hub != nullptr) {
          status_hub->register_conn(ctx->workbook_id, StatusWsHub::Channel::Containers, &conn);
        }
        conn.send_text(containers_json(*activity, ctx->workbook_id).dump());
      })
      .onclose([status_hub](crow::websocket::connection& conn, const std::string&, std::uint16_t) {
        if (status_hub != nullptr) {
          status_hub->unregister_conn(&conn);
        }
        release_ws_auth(conn);
      });
}

}  // namespace algocraft::api
