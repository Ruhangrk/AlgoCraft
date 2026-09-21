#include "algocraft/api/workbook_routes.hpp"

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "algocraft/backtest/backtest_service.hpp"
#include "algocraft/domain/capital.hpp"
#include "algocraft/domain/ids.hpp"
#include "algocraft/domain/quantity.hpp"
#include "algocraft/domain/timestamp.hpp"
#include "algocraft/engine/run_manager.hpp"

namespace algocraft::api {
namespace {

struct WsAuth {
  AuthTokenClaims claims;
  std::int64_t workbook_id{};
};

std::int64_t uuid_low(const Uuid& id) {
  std::uint64_t val = 0;
  for (int i = 0; i < 8; ++i) {
    val = (val << 8) | id.bytes[static_cast<std::size_t>(8 + i)];
  }
  return static_cast<std::int64_t>(val);
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

crow::json::wvalue containers_json(ActivityRepository& activity, std::int64_t wid) {
  crow::json::wvalue root = crow::json::wvalue::list();
  std::size_t i = 0;
  for (const auto& run : activity.list_runs(wid)) {
    for (const auto& c : activity.list_containers(run.id)) {
      root[i]["id"] = c.id;
      root[i]["ticker"] = c.ticker;
      root[i]["strategy"] = c.strategy_name;
      root[i]["mode"] = c.mode;
      root[i]["fills"] = c.fills;
      root[i]["realized_paise"] = c.realized_paise;
      ++i;
    }
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
  char* end = nullptr;
  const auto wid = std::strtoll(std::string(id_part).c_str(), &end, 10);
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

  CROW_ROUTE(app, "/workbooks/<int>/runs/start")
      .methods(crow::HTTPMethod::POST)([auth, workbooks, activity, data, strategies,
                                        symbols](const crow::request& req, std::int64_t wid) {
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
        cfg.tickers = json_string_array(body, "tickers");
        if (cfg.tickers.empty()) {
          cfg.tickers = {"RELIANCE"};
        }
        cfg.strategies = json_string_array(body, "strategies");
        if (cfg.strategies.empty()) {
          cfg.strategies = {"ema_crossover"};
        }
        cfg.router = json_string_or(body, "router", "default_router");
        cfg.from =
            Timestamp::from_nanos(json_int_or(body, "from_ns", 1'788'921'000'000'000'000LL));
        cfg.to = Timestamp::from_nanos(json_int_or(body, "to_ns", 1'789'064'940'000'000'000LL));
        cfg.trade_from =
            Timestamp::from_nanos(json_int_or(body, "trade_from_ns", 1'789'065'000'000'000'000LL));
        cfg.trade_to =
            Timestamp::from_nanos(json_int_or(body, "trade_to_ns", 1'789'151'340'000'000'000LL));

        for (const auto& t : cfg.tickers) {
          symbols->intern({.ticker = t}, {});
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
        root["run_id"] = runs.empty() ? 0 : runs.back().id;
        root["workbook_id"] = wid;
        root["selected"] = result.selected;
        root["skipped"] = result.skipped;
        root["fills"] = result.fills;
        root["returned_paise"] = result.returned.paise();
        root["signals"] = static_cast<std::int64_t>(result.signals.size());
        root["rejections"] = static_cast<std::int64_t>(result.rejections.size());
        return json_ok(std::move(root), 201);
      });

  CROW_ROUTE(app, "/workbooks/<int>/runs")
      .methods(crow::HTTPMethod::GET)([auth, workbooks, activity](const crow::request& req,
                                                                  std::int64_t wid) {
        auto gate = require_workbook(*auth, *workbooks, req, wid);
        if (!gate) {
          return std::move(gate.error);
        }
        return json_ok(runs_json(activity->list_runs(wid)));
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
        bt.capital = Capital::from_paise(json_int_or(body, "capital_paise", wb->available_paise));
        bt.from = Timestamp::from_nanos(json_int_or(body, "from_ns", 0));
        bt.to = Timestamp::from_nanos(json_int_or(body, "to_ns", 0));
        if (bt.from.nanos() == 0 || bt.to.nanos() == 0) {
          return json_error(400, "from_ns and to_ns are required");
        }
        bt.strategy.order_qty =
            Quantity::from_shares(json_int_or(body, "order_qty", 1));
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
        return json_ok(backtests_json(backtests->list_for_workbook(wid)));
      });

  CROW_ROUTE(app, "/workbooks/<int>/backtests/<int>")
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

  CROW_WEBSOCKET_ROUTE(app, "/ws/workbooks/<path>/portfolio")
      .onaccept([ws_accept](const crow::request& req, void** userdata) {
        return ws_accept(req, userdata, "portfolio");
      })
      .onopen([workbooks](crow::websocket::connection& conn) {
        auto* ctx = static_cast<WsAuth*>(conn.userdata());
        if (ctx == nullptr) {
          return;
        }
        if (const auto row = workbooks->find(ctx->workbook_id)) {
          conn.send_text(portfolio_json(ctx->workbook_id, *row).dump());
        }
      })
      .onclose([](crow::websocket::connection& conn, const std::string&, std::uint16_t) {
        delete static_cast<WsAuth*>(conn.userdata());
      });

  CROW_WEBSOCKET_ROUTE(app, "/ws/workbooks/<path>/containers")
      .onaccept([ws_accept](const crow::request& req, void** userdata) {
        return ws_accept(req, userdata, "containers");
      })
      .onopen([activity](crow::websocket::connection& conn) {
        auto* ctx = static_cast<WsAuth*>(conn.userdata());
        if (ctx == nullptr) {
          return;
        }
        conn.send_text(containers_json(*activity, ctx->workbook_id).dump());
      })
      .onclose([](crow::websocket::connection& conn, const std::string&, std::uint16_t) {
        delete static_cast<WsAuth*>(conn.userdata());
      });
}

}  // namespace algocraft::api
