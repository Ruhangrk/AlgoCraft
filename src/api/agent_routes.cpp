#include "algocraft/api/agent_routes.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <sys/types.h>
#include <unistd.h>

#include "algocraft/log/log.hpp"
#include "algocraft/persistence/strategy_catalog_repository.hpp"
#include "algocraft/strategies/strategy_compiler.hpp"

namespace algocraft::api {
namespace {

std::atomic<bool> g_reload_scheduled{false};

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

// Detach scripts/reload_engine so activate can return before build+restart.
// Returns: "scheduled" | "already_scheduled" | "missing_script" | "fork_failed"
std::string schedule_engine_reload(const std::filesystem::path& source_root) {
  if (source_root.empty()) {
    return "missing_script";
  }
  const auto script = source_root / "scripts" / "reload_engine";
  if (!std::filesystem::exists(script)) {
    AC_LOG_ERROR("agent_reload script missing path={}", script.string());
    return "missing_script";
  }

  bool expected = false;
  if (!g_reload_scheduled.compare_exchange_strong(expected, true)) {
    AC_LOG_INFO("agent_reload already scheduled; skipping duplicate");
    return "already_scheduled";
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    g_reload_scheduled.store(false);
    AC_LOG_ERROR("agent_reload fork failed");
    return "fork_failed";
  }
  if (pid == 0) {
    // Child: new session so we survive parent exit when serve is killed.
    ::setsid();
    // Best-effort: avoid inheriting Crow listen sockets as busy fds.
    for (int fd = 3; fd < 256; ++fd) {
      ::close(fd);
    }
    const std::string path = script.string();
    ::execl(path.c_str(), path.c_str(), "info", static_cast<char*>(nullptr));
    std::_Exit(127);
  }

  AC_LOG_INFO("agent_reload scheduled pid={} script={}", static_cast<int>(pid), script.string());
  return "scheduled";
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
        // Crow has no 422 mapping (it coerces unknown codes to 500); use 400 for compile fail.
        return json_ok(std::move(root), compiled.ok ? 200 : 400);
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
          // Crow has no 422 mapping (it coerces unknown codes to 500); use 400 for promote fail.
          return json_ok(std::move(root), 400);
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
            "catalog enabled=0; call POST /agent/strategies/activate "
            "(activate schedules rebuild+restart)";
        AC_LOG_INFO("api_agent_promote name={} id={}", name, id);
        return json_ok(std::move(root), 201);
      });

  CROW_ROUTE(app, "/agent/strategies/activate")
      .methods(crow::HTTPMethod::POST)([auth, persist, source_root](const crow::request& req) {
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
        // Activate and deactivate both rebuild+restart so the running binary matches catalog intent.
        const auto reload = schedule_engine_reload(source_root);
        root["reload"] = reload;
        if (reload == "scheduled") {
          root["note"] = enabled
                             ? "enabled=1; rebuild+restart scheduled (scripts/reload_engine). "
                               "API will drop briefly; poll GET /strategies when serve is back"
                             : "enabled=0; rebuild+restart scheduled (scripts/reload_engine). "
                               "API will drop briefly";
        } else if (reload == "already_scheduled") {
          root["note"] = enabled ? "enabled=1; engine reload already in progress"
                                 : "enabled=0; engine reload already in progress";
        } else {
          root["note"] =
              std::string(enabled ? "enabled=1" : "enabled=0") +
              "; failed to schedule reload (" + reload +
              "); run: cmake --build build -j2 --target algocraft_engine && scripts/serve --restart";
        }
        AC_LOG_INFO("api_agent_activate name={} enabled={} reload={}", name, enabled, reload);
        return json_ok(std::move(root));
      });
}

}  // namespace algocraft::api
