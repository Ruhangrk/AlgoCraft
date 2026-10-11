# Agent strategy APIs — Python usage guide

How AlgoCraft-Agent (or any HTTP client) creates a new C++ strategy via AlgoCraft.

Base URL: `http://127.0.0.1:8080`  
Auth: `Authorization: Bearer <token>` on all `/agent/*` routes.

---

## End-to-end flow

```text
1. JWT          POST /auth/login
2. Compile loop POST /agent/strategies/compile   (until ok=true, max ~5)
3. Human OK
4. Promote      POST /agent/strategies/promote   → enabled=0, files on disk
5. Human OK
6. Activate     POST /agent/strategies/activate  → enabled=1
                                              → schedules rebuild + restart
```

On `activate` with `enabled=true`, AlgoCraft schedules `scripts/reload_engine` (detached):

1. sleep ~1s (so the HTTP response can flush)
2. `cmake --build build -j2 --target algocraft_engine`
3. `scripts/serve --restart`

Response includes `"reload": "scheduled"` (or `already_scheduled` / error). The API drops briefly during restart; poll `GET /strategies` when serve is back.

Deactivate (`enabled=false`) also schedules the same rebuild+restart.

---

## 1) Auth

```http
POST /auth/login
{"username":"...","password":"..."}
```

Response `200`: `{ "token": "...", "user": ... }`.

Prefer forwarding the **end-user JWT** from UI → Agent → C++. Fallback: login as `ALGOCRAFT_USER` / `ALGOCRAFT_PASS` for CLI smoke only.

---

## 2) Compile (sandbox only)

```http
POST /agent/strategies/compile
Authorization: Bearer <token>
Content-Type: application/json

{
  "name": "my_mean_revert",
  "class_name": "MyMeanRevert",
  "kind": "strategy",
  "hpp": "#pragma once\n...",
  "cpp": "#include \"algocraft/strategies/my_mean_revert.hpp\"\n..."
}
```

| Result | HTTP | Body |
|--------|------|------|
| Success | **200** | `{ok: true, name, class_name, sandbox_dir, log}` |
| Failure | **400** | `{ok: false, name, class_name, sandbox_dir, log}` |

**Important:** Crow has no `422` status mapping (unknown codes become `500`). Compile/promote failures use **`400` + `ok: false`**. Python must treat that as a soft failure and read `log` for the fix loop — do not only check for `422`.

**Name:** `^[a-z][a-z0-9_]{0,63}$`  
**Kind:** only `"strategy"` in v1.  
**Sandbox:** `data/agent_sandbox/<name>/` on the C++ host. No catalog enable on compile.  
**Timeout:** ~180s recommended.

### Python compile loop (recommended)

```python
async def compile_until_ok(client, name, class_name, hpp, cpp, max_attempts=5):
    for attempt in range(1, max_attempts + 1):
        try:
            resp = await client.agent_compile(name, hpp, cpp, class_name)
            # HTTP 200 path
            if resp.get("ok"):
                return resp
            log = resp.get("log", "")
        except AlgocraftApiError as e:
            body = e.body if isinstance(e.body, dict) else {}
            # MUST accept 400 (current C++). Keep 422 for old mocks/tests.
            if e.status in (400, 422) or body.get("ok") is False:
                log = body.get("log", str(e.body))
            else:
                raise
        # LLM fix using `log` → new hpp/cpp
        hpp, cpp = await llm_fix(hpp, cpp, log, attempt)
    raise RuntimeError("compile failed after max attempts")
```

If your client raises on every non-2xx, catch `AlgocraftApiError` and branch on status / `ok:false` as above. Update `compile_loop` if it still only handles `422`.

---

## 3) What code to send (`hpp` / `cpp`)

### Rules the LLM must follow

- `metadata().name` **==** `name` (snake_case)
- Class `final : public Strategy` in `namespace algocraft`
- Methods: `configure`, `on_bar`, `on_fill`, `on_order_update`, `should_exit`, `metadata`
- Default: `TradingMode::Mis`, `BarResolution::OneMin`
- Include: `#include "algocraft/strategies/<name>.hpp"`
- Qty: `Quantity::from_shares(1)` — **never** `Quantity{1}` (private ctor)
- Money: `.paise()` ints; time: `ist_minute_of_day` / nanos
- Do **not** emit CMake, registry patches, routers, or build systems

Agent knowledge docs (selective fetch): `AlgoCraft-Agent/docs/cpp/INDEX.md` → ≤2–3 files (`codegen_shape`, `strategy_interface`, …).

### Minimal valid example

**hpp**

```cpp
#pragma once
#include "algocraft/strategies/strategy.hpp"
namespace algocraft {
class MyMeanRevert final : public Strategy {
public:
  void configure(const StrategyConfig&, IndicatorLibrary&) override;
  void on_bar(const BarEvent&, const PortfolioView&, std::vector<OrderIntent>&) override;
  void on_fill(const FillEvent&) override {}
  void on_order_update(const OrderUpdate&) override {}
  bool should_exit() const override { return false; }
  StrategyMetadata metadata() const override;
private:
  StrategyConfig config_{};
};
}
```

**cpp**

```cpp
#include "algocraft/strategies/my_mean_revert.hpp"
#include "algocraft/domain/bar_resolution.hpp"
#include "algocraft/strategies/make_intent.hpp"
namespace algocraft {
namespace { constexpr StrategyId kSid = StrategyId::from(99); }
void MyMeanRevert::configure(const StrategyConfig& c, IndicatorLibrary&) { config_ = c; }
void MyMeanRevert::on_bar(const BarEvent& bar, const PortfolioView& pf,
                          std::vector<OrderIntent>& out) {
  if (bar.symbol_id != config_.symbol_id || bar.resolution != BarResolution::OneMin) return;
  if (pf.position.shares() == 0)
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Buy, Quantity::from_shares(1)));
  else
    out.push_back(make_intent(kSid, config_.symbol_id, Side::Sell, pf.position));
}
StrategyMetadata MyMeanRevert::metadata() const {
  return {"my_mean_revert", "1.0.0", TradingMode::Mis, BarResolution::OneMin, {}};
}
}
```

---

## 4) Promote (human-gated)

Only after successful compile (`ok: true`) **and** human confirmation.

```http
POST /agent/strategies/promote
{"name": "my_mean_revert"}
```

Success `201`:

```json
{
  "ok": true,
  "id": 1,
  "name": "my_mean_revert",
  "class_name": "MyMeanRevert",
  "enabled": false,
  "hpp_path": "include/algocraft/strategies/my_mean_revert.hpp",
  "cpp_path": "src/strategies/my_mean_revert.cpp",
  "log": "...",
  "note": "catalog enabled=0; call POST /agent/strategies/activate then rebuild+restart serve"
}
```

Failure (no sandbox): **400** `{ok: false, log: "sandbox sources missing; call /agent/strategies/compile first"}`.

C++ side:

- Writes `include/algocraft/strategies/<name>.hpp` and `src/strategies/<name>.cpp`
- Patches `CMakeLists.txt` and `strategy_registrations.cpp`
- Upserts `strategy_catalog` with **`enabled=0`**

Never auto-promote without human OK.

---

## 5) Activate (second human gate)

```http
POST /agent/strategies/activate
{"name": "my_mean_revert", "enabled": true}
```

Success `200`: catalog row + `ok: true` + `reload` + `note`.  
Both `enabled=true` and `enabled=false` schedule rebuild+restart (`reload` usually `"scheduled"`; log: `data/logs/reload_engine.log`).  
Missing name: **404** `{error: "strategy not in catalog; promote first"}`.

---

## 6) Catalog (optional)

```http
GET /agent/strategies/catalog
```

Response: array of rows — `id`, `kind`, `name`, `class_name`, `enabled`, `hpp_path`, `cpp_path`, `sandbox_path`, `provenance`, `compile_ok`, timestamps.

---

## Python client methods (AlgoCraft-Agent)

| Method | Endpoint |
|--------|----------|
| `agent_compile(name, hpp, cpp, class_name="")` | `POST /agent/strategies/compile` |
| `agent_promote(name)` | `POST /agent/strategies/promote` |
| `agent_activate(name, enabled=True)` | `POST /agent/strategies/activate` |
| `agent_catalog()` | `GET /agent/strategies/catalog` |

---

## Do / don’t

| Do | Don’t |
|----|--------|
| Loop compile on `ok:false` + `log` (HTTP **400**) | Treat only `422` as compile failure |
| Human gate before promote **and** activate | Auto-promote / auto-activate |
| Use `Quantity::from_shares` | Use `Quantity{1}` |
| Keep `metadata.name == name` | Emit routers / CMake yourself |
| After activate, wait for reload (`reload=scheduled`) then poll `/strategies` | Expect zero-downtime hot `dlopen` |

---

## Related

- Build architecture: `Notes/AI_AGENT.md`
- Agent-facing C++ slices: `AlgoCraft-Agent/docs/cpp/INDEX.md`
- Implementation: `src/api/agent_routes.cpp`, `src/strategies/strategy_compiler.cpp`
