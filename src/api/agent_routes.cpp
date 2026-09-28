#include "algocraft/api/agent_routes.hpp"

#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "algocraft/log/log.hpp"
#include "algocraft/persistence/strategy_catalog_repository.hpp"
#include "algocraft/strategies/strategy_compiler.hpp"

namespace algocraft::api {
namespace {

crow::json::wvalue catalog_json(const StrategyCatalogRepository::Row& row) {
  crow::json::wvalue root;
  root["id"] = row.id;
  root["kind"] = row.kind;
  root["name"] = row.name;
  root["class_name"] = row.class_name;
  root["enabled"] = row.enabled;
  root["hpp_path"] = row.hpp_path;
  root["cpp_path"] = row.cpp_path;
  root["sandbox_path"] = row.sandbox_path;
  root["provenance"] = row.provenance;
  root["compile_ok"] = row.compile_ok;
  root["created_at"] = row.created_at;
  root["updated_at"] = row.updated_at;
  return root;
}

}  // namespace

void register_agent_routes(App& app, AgentRouteDeps deps) {
  auto* auth = &deps.auth;
  auto* persist = deps.persist;
  const auto source_root = deps.source_root;
  auto sandbox_root = deps.sandbox_root;
  if (sandbox_root.empty()) {
    sandbox_root = source_root / "data" / "agent_sandbox";
  }

  CROW_ROUTE(app, "/agent/strategies/catalog")
      .methods(crow::HTTPMethod::GET)([auth, persist](const crow::request& req) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (persist == nullptr) {
          return json_error(503, "persistence not configured");
        }
        std::vector<StrategyCatalogRepository::Row> rows;
        persist->run_sync([&](sqlite3* db) { rows = StrategyCatalogRepository(db).list_all(); });
        crow::json::wvalue root = crow::json::wvalue::list();
        for (std::size_t i = 0; i < rows.size(); ++i) {
          root[i] = catalog_json(rows[i]);
        }
        return json_ok(std::move(root));
      });

  CROW_ROUTE(app, "/agent/strategies/compile")
      .methods(crow::HTTPMethod::POST)([auth, persist, source_root,
                                        sandbox_root](const crow::request& req) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (persist == nullptr) {
          return json_error(503, "persistence not configured");
        }
        if (source_root.empty()) {
          return json_error(500, "source_root not configured");
        }

        const auto body = body_or_empty(req);
        StrategyCompiler::Request creq{};
        creq.name = json_string_or(body, "name", "");
        creq.class_name = json_string_or(body, "class_name", "");
        creq.hpp = json_string_or(body, "hpp", "");
        creq.cpp = json_string_or(body, "cpp", "");
        creq.kind = json_string_or(body, "kind", "strategy");
        if (creq.kind != "strategy") {
          return json_error(400, "only kind=strategy supported in v1");
        }

        StrategyCompiler compiler({.source_root = source_root, .sandbox_root = sandbox_root});
        const auto compiled = compiler.compile(creq);

        persist->run_sync([&](sqlite3* db) {
          (void)StrategyCatalogRepository(db).set_compile_ok(creq.name, compiled.ok,
                                                             compiled.sandbox_dir.string());
        });

        crow::json::wvalue root;
        root["ok"] = compiled.ok;
        root["name"] = creq.name;
        root["class_name"] = compiled.class_name;
        root["sandbox_dir"] = compiled.sandbox_dir.string();
        root["log"] = compiled.log;
        AC_LOG_INFO("api_agent_compile name={} ok={}", creq.name, compiled.ok);
        return json_ok(std::move(root), compiled.ok ? 200 : 422);
      });

  CROW_ROUTE(app, "/agent/strategies/promote")
      .methods(crow::HTTPMethod::POST)([auth, persist, source_root,
                                        sandbox_root](const crow::request& req) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (persist == nullptr) {
          return json_error(503, "persistence not configured");
        }
        if (source_root.empty()) {
          return json_error(500, "source_root not configured");
        }

        const auto body = body_or_empty(req);
        const auto name = json_string_or(body, "name", "");
        if (!StrategyCompiler::valid_name(name)) {
          return json_error(400, "invalid or missing name");
        }

        StrategyCompiler compiler({.source_root = source_root, .sandbox_root = sandbox_root});
        const auto promoted = compiler.promote(name);
        if (!promoted.ok) {
          crow::json::wvalue root;
          root["ok"] = false;
          root["name"] = name;
          root["log"] = promoted.log;
          return json_ok(std::move(root), 422);
        }

        StrategyCatalogRepository::Row row{};
        row.kind = "strategy";
        row.name = name;
        row.class_name = promoted.class_name;
        row.enabled = false;
        row.hpp_path = promoted.hpp_path.string();
        row.cpp_path = promoted.cpp_path.string();
        row.sandbox_path = (sandbox_root / name).string();
        row.provenance = "agent";
        row.compile_ok = true;

        std::int64_t id = 0;
        persist->run_sync([&](sqlite3* db) {
          id = StrategyCatalogRepository(db).upsert_promoted(row);
        });

        crow::json::wvalue root;
        root["ok"] = true;
        root["id"] = id;
        root["name"] = name;
        root["class_name"] = promoted.class_name;
        root["enabled"] = false;
        root["hpp_path"] = promoted.hpp_path.string();
        root["cpp_path"] = promoted.cpp_path.string();
        root["log"] = promoted.log;
        root["note"] =
            "catalog enabled=0; call POST /agent/strategies/activate then rebuild+restart serve";
        AC_LOG_INFO("api_agent_promote name={} id={}", name, id);
        return json_ok(std::move(root), 201);
      });

  CROW_ROUTE(app, "/agent/strategies/activate")
      .methods(crow::HTTPMethod::POST)([auth, persist](const crow::request& req) {
        auto gate = require_user(*auth, req);
        if (!gate) {
          return std::move(gate.error);
        }
        if (persist == nullptr) {
          return json_error(503, "persistence not configured");
        }

        const auto body = body_or_empty(req);
        const auto name = json_string_or(body, "name", "");
        if (name.empty()) {
          return json_error(400, "name required");
        }
        // Default true; pass enabled=false / enabled=0 to deactivate.
        bool enabled = true;
        if (body.has("enabled")) {
          if (body["enabled"].t() == crow::json::type::False) {
            enabled = false;
          } else if (body["enabled"].t() == crow::json::type::True) {
            enabled = true;
          } else {
            enabled = json_int_or(body, "enabled", 1) != 0;
          }
        }

        bool found = false;
        std::optional<StrategyCatalogRepository::Row> row;
        persist->run_sync([&](sqlite3* db) {
          StrategyCatalogRepository repo(db);
          found = repo.set_enabled(name, enabled);
          row = repo.find_by_name(name);
        });

        if (!found || !row) {
          return json_error(404, "strategy not in catalog; promote first");
        }

        crow::json::wvalue root = catalog_json(*row);
        root["ok"] = true;
        root["note"] = enabled
                           ? "enabled=1; rebuild engine and restart serve to load new C++ into registry"
                           : "enabled=0; strategy hidden from agent catalog consumers";
        AC_LOG_INFO("api_agent_activate name={} enabled={}", name, enabled);
        return json_ok(std::move(root));
      });
}

}  // namespace algocraft::api
