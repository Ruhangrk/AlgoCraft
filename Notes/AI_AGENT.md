# AlgoCraft-Agent — Build Spec (for implementers / Cursor)

**Repo to create:** `AlgoCraft-Agent` (sibling of `AlgoCraft`)  
**Stack:** Python 3.11+ · FastAPI · LangGraph · httpx · Pydantic v2 · SSE  
**Copy this file to** `AlgoCraft-Agent/ARCHITECTURE.md` **when the repo is created.**

**Status (2026-09-28):**
- C++ engine APIs for Phase A **and** B/C compile/promote/activate are **implemented** in AlgoCraft.
- Python agent code: **not started**. This doc is the full build plan.

**Non-goals (v1):** auto live trading without human approval; LLM shell/git access; embedding bars/risk in Python.

---

## 0. How to use this doc (Cursor cascade)

Build **in order**. Do not skip Phase A for B/C.

1. Create repo + skeleton (§7, §13 step 1–2)  
2. `AlgocraftClient` against live `:8080` (§6)  
3. LangGraph Phase A with mocks, then real tools (§4, §13)  
4. FastAPI chat + SSE (§5)  
5. Phase B compile loop (§4.5, §6.2)  
6. Phase C promote/activate with **human confirmation tool** (§4.6, §6.2)  

**Prereq:** AlgoCraft `scripts/serve` running; user registered; JWT works.

---

## 1. Purpose

Chat orchestrator that:

1. Parses a trading idea  
2. Researches via C++ HTTP (`/instruments`, `/strategies`, `/routing-algos`)  
3. Proposes an existing strategy/router **or** (Phase B) generates C++ sources  
4. Runs **real** backtests / hist runs on AlgoCraft  
5. Evaluates metrics (soft thresholds)  
6. Asks human before promote/activate  

Python = brain + HTTP tools. C++ = capital, bars, fills, risk, compile sandbox.

---

## 2. Placement

```
AlgoCraft-UI (:5173)
    ├── JWT ──► AlgoCraft C++ (:8080)
    └── JWT ──► AlgoCraft-Agent (:8100)
                    └── httpx + Bearer ──► AlgoCraft C++ (:8080)
```

| Rule | Locked decision |
|------|-----------------|
| Auth to C++ | **Forward the end-user JWT** from UI→Agent→C++ (`Authorization: Bearer …`). Fallback: login as `ALGOCRAFT_USER` / `ALGOCRAFT_PASS` only for CLI smoke. |
| Workbook | Session may include `workbook_id`; else `POST /workbooks` once and store id. |
| Default run mode | **Hist** with past `anchor_date` (not live today) unless user explicitly asks live. |
| Tool timeout | **600s** for `runs/start` / heavy backtests; **120s** for light GETs; compile **180s**. |
| Eval | Soft verdict (pass/fail/weak) — do **not** hard-stop only because `pnl <= 0`. |
| Concurrency | 1 uvicorn worker; max 2 concurrent graphs. |

---

## 3. Phases

### Phase A — Chat + existing strategies (ship first)
- No codegen. Pick from `GET /strategies` / `GET /routing-algos`.
- Tools: auth, list, instruments, ensure, workbook, backtest, hist run, events.

### Phase B — Codegen + compile loop
- LLM emits `name` (snake_case), `hpp`, `cpp` matching AlgoCraft `Strategy` API.
- Tool: `POST /agent/strategies/compile` until `ok=true` (max compile attempts = 5).
- **Do not** promote without human OK.

### Phase C — Promote + activate
- Human OK → `POST /agent/strategies/promote` (`enabled=0` in catalog).
- Second human OK → `POST /agent/strategies/activate` (`enabled=1`).
- Tell user: **rebuild + `scripts/serve --restart`** required for new C++ to load into the in-process registry (no dlopen yet).

---

## 4. LangGraph

### 4.1 Phase A graph

```
START → classify_intent → research → propose → execute → evaluate
                              ↑                      │
                              └──── iterate < max ────┘ (verdict=fail/weak)
                                                    │
                                                    ▼
                                                 respond → END
```

Use `langgraph.graph.StateGraph`. Conditional edges from `evaluate`:
- `pass` | `need_human` → `respond`
- `fail` / `weak` and `iteration < max_iterations` → `propose` (increment `iteration`, set `feedback`)
- else → `respond`

### 4.2 Phase B/C extension (same graph, extra intent)

If intent = `codegen_strategy`:

```
research → design_code → compile_loop → (human_gate_promote) → promote
        → (human_gate_activate) → activate → respond
```

`compile_loop`: call compile; if not ok, LLM fixes from `log`; repeat ≤ 5.

### 4.3 AgentState (Pydantic preferred)

```python
from typing import Annotated, Any, Literal
from typing_extensions import TypedDict
from langgraph.graph.message import add_messages

class AgentState(TypedDict, total=False):
    messages: Annotated[list, add_messages]
    session_id: str
    user_jwt: str                    # forwarded to C++
    workbook_id: int | None

    intent: Literal[
        "research", "backtest", "route", "codegen_strategy", "explain", "unknown"
    ]
    tickers: list[str]
    strategy_candidates: list[str]
    router_candidates: list[str]
    research_notes: str

    chosen: dict[str, Any]
    # backtest: {strategy, ticker, from_ns, to_ns, capital_paise}
    # route:    {router, anchor_date, capital_paise}  # hist preferred
    # codegen:  {name, class_name, hpp, cpp}

    cpp_results: dict[str, Any]
    metrics: dict[str, Any]
    eval_verdict: Literal["pass", "weak", "fail", "need_human", "error"]
    feedback: str
    iteration: int
    max_iterations: int              # default 3
    compile_attempts: int            # default 0, max 5
    pending_human: Literal["none", "promote", "activate"]
    error: str | None
```

### 4.4 Node contracts (implement these files)

| Node | File | Must do |
|------|------|---------|
| `classify_intent` | `nodes/classify.py` | LLM or rules → `intent` |
| `research` | `nodes/research.py` | `list_strategies`, `list_routers`, optional `search_instruments`, `ensure_market_data` |
| `propose` | `nodes/propose.py` | LLM constrained to **exact** names from C++; fill `chosen` |
| `execute` | `nodes/execute.py` | `start_backtest` **or** `start_run` (hist); store raw JSON in `cpp_results` |
| `evaluate` | `nodes/evaluate.py` | Pure Python thresholds → `eval_verdict`, `metrics`, `feedback` |
| `respond` | `nodes/respond.py` | LLM summary + structured `card` JSON for UI |
| `design_code` | `nodes/design_code.py` | Phase B: emit Strategy sources |
| `compile_loop` | `nodes/compile_loop.py` | Phase B: compile + fix |
| `human_gate` | `nodes/human_gate.py` | Interrupt / wait for UI `confirm=true` before promote/activate |

### 4.5 Evaluate thresholds (env)

```text
AGENT_MAX_ITERATIONS=3
AGENT_MIN_FILLS=2
AGENT_MAX_FILLS=5000
AGENT_SOFT_MIN_PNL_PAISE=1      # pnl >= this → "pass"; else if fills ok → "weak"; else "fail"
```

Logic sketch:
```python
fills = metrics.get("fills", 0)
pnl = metrics.get("pnl_paise", 0)
if fills < min_fills:
    verdict = "fail"
elif pnl >= soft_min_pnl:
    verdict = "pass"
else:
    verdict = "weak"   # still show results; allow iterate
```

### 4.6 Human gates (Phase C)

Do **not** auto-promote. Patterns:
- LangGraph `interrupt()` / wait for next chat message `APPROVE_PROMOTE` / `APPROVE_ACTIVATE`, **or**
- FastAPI `POST /v1/sessions/{id}/confirm` with `{action: "promote"|"activate"}`.

Agent message must show: strategy name, compile log summary, paths, risk note (restart required).

---

## 5. FastAPI surface (Python)

| Method | Path | Body / notes |
|--------|------|----------------|
| `GET` | `/healthz` | `{status, cpp_reachable}` |
| `POST` | `/v1/sessions` | `{workbook_id?}` + header `Authorization` (user JWT) → `{session_id}` |
| `GET` | `/v1/sessions/{id}` | history + last metrics/card |
| `POST` | `/v1/chat` | `{session_id, message}` → runs graph (sync JSON) **or** kicks async job |
| `GET` | `/v1/chat/{session_id}/stream` | **SSE**: `event: token\|tool\|card\|error\|done` |
| `POST` | `/v1/sessions/{id}/confirm` | `{action: "promote"\|"activate"}` |

Sessions: **in-memory dict** v1 (`SessionStore`); optional SQLite later.  
CORS: allow `http://127.0.0.1:5173`.

---

## 6. C++ HTTP contracts (exact)

Base: `ALGOCRAFT_API_URL` default `http://127.0.0.1:8080`.  
All mutating/agent routes need `Authorization: Bearer <jwt>` except `/auth/login` and `/auth/register`.

### 6.1 Phase A tools

| Tool | Request | Response highlights |
|------|---------|---------------------|
| `login` | `POST /auth/login` `{"username","password"}` | `{token}` or equivalent — store JWT |
| `list_strategies` | `GET /strategies` | list/array of names |
| `list_routers` | `GET /routing-algos` | includes `default_router`, `top15_week_router`, `live_run_testing_router` |
| `search_instruments` | `GET /instruments?q=ONGC&limit=20` | rows with ticker |
| `ensure_market_data` | `POST /market-data/ensure` body per UI (tickers + range) | results |
| `create_workbook` | `POST /workbooks` `{"name","capital_paise"}` | `{id}` |
| `start_backtest` | `POST /workbooks/{wid}/backtests/start` | **required:** `ticker`, `strategy`, `from_ns`, `to_ns`, `capital_paise`; optional `order_qty`, ema_* | `201` + row (`id`, `fills`, `pnl_paise`, …) **sync** |
| `get_backtest` | `GET /workbooks/{wid}/backtests/{id}` | same row |
| `start_run` | `POST /workbooks/{wid}/runs/start` | `{router, capital_paise, anchor_date:"YYYY-MM-DD"}` for hist/live; **prefer past day for agent** | `201` + `run_id`, `selected`, `fills`, … |
| `run_events` | `GET /workbooks/{wid}/runs/{rid}/events?include=routing,fill` | event list |

**Time helper (must implement in Python):**

```python
# IST session day → nanos (match AlgoCraft session_calendar: IST = UTC+5:30)
# from_ns = IST midnight start of day as UTC epoch nanos
# to_ns   = IST 23:59 end (or next day 00:00 - 1m) as used by UI
```

Prefer computing from `zoneinfo.ZoneInfo("Asia/Kolkata")`.  
“Last week” → last 5 NSE session days ending at last **closed** session (not today if still open).

**Hist run tip:** send `anchor_date` for a **past** trading day so path is hist replay, not live.

### 6.2 Phase B/C agent tools (implemented in C++)

All under `/agent/strategies/*`, JWT required.

#### `POST /agent/strategies/compile`
```json
{
  "name": "my_mean_revert",
  "class_name": "MyMeanRevert",
  "kind": "strategy",
  "hpp": "#pragma once\n...",
  "cpp": "#include \"algocraft/strategies/my_mean_revert.hpp\"\n..."
}
```
- `name`: `^[a-z][a-z0-9_]{0,63}$`
- Response `200` if ok, `422` if compile failed: `{ok, name, class_name, sandbox_dir, log}`
- Sandbox on disk: `data/agent_sandbox/<name>/` (C++ side). No catalog enable.

#### `POST /agent/strategies/promote`
```json
{ "name": "my_mean_revert" }
```
- Requires prior successful compile artifacts in sandbox.
- Writes `include/algocraft/strategies/<name>.hpp`, `src/strategies/<name>.cpp`
- Patches `CMakeLists.txt` + `strategy_registrations.cpp`
- Upserts `strategy_catalog` with **`enabled=0`**
- `201`: `{ok, id, name, class_name, enabled:false, hpp_path, cpp_path, log, note}`

#### `POST /agent/strategies/activate`
```json
{ "name": "my_mean_revert", "enabled": true }
```
- `enabled: false` deactivates.
- `200`: catalog row + note to rebuild/restart.

#### `GET /agent/strategies/catalog`
- Array of catalog rows (`id`, `kind`, `name`, `class_name`, `enabled`, paths, `compile_ok`, …).

### 6.3 Generated strategy shape (Phase B LLM must follow)

Mirror existing strategies (e.g. `live_run_testing`, `hammer_reversal`):

- Class `final : public Strategy` in namespace `algocraft`
- Methods: `configure`, `on_bar`, `on_fill`, `on_order_update`, `should_exit`, `metadata`
- `metadata().name` == snake `name`
- `TradingMode::Mis`, `BarResolution::OneMin` unless user asks otherwise
- Include `make_intent.hpp`, `session_clock.hpp`, `position_sizer.hpp` as needed
- Header path: `#include "algocraft/strategies/<name>.hpp"`

Do **not** invent new build systems; C++ compile API only syntax-checks with project `-Iinclude`.

---

## 7. Repo layout (create exactly)

```
AlgoCraft-Agent/
  ARCHITECTURE.md          ← copy of this file
  README.md
  pyproject.toml
  .env.example
  docs/
    cpp/
      INDEX.md             ← always in LLM context (path + one-line about)
      *.md                 ← core contracts; fetch ≤2–3 (see §7.1)
      strategies/          ← one md per registered strategy + INDEX.md
      indicators/          ← library + each indicator + INDEX.md
      routing/             ← interface + each router + INDEX.md
  app/
    __init__.py
    main.py                # FastAPI app + CORS + routers
    config.py              # pydantic-settings from env
    api/
      __init__.py
      health.py
      sessions.py
      chat.py
      confirm.py
    graph/
      __init__.py
      state.py
      graph.py             # build_graph() → compiled LangGraph
      nodes/
        classify.py
        research.py
        propose.py
        execute.py
        evaluate.py
        respond.py
        design_code.py
        compile_loop.py
        human_gate.py
    tools/
      __init__.py
      algocraft_client.py  # httpx.AsyncClient, Bearer, timeouts
      market.py
      backtest.py
      routing.py
      agent_lifecycle.py   # compile/promote/activate/catalog
      timeutil.py          # IST ↔ nanos
    llm/
      provider.py          # chat model from LLM_PROVIDER
      prompts.py
    store/
      sessions.py          # in-memory SessionStore
    models/
      api.py               # request/response schemas
  tests/
    conftest.py
    test_timeutil.py
    test_evaluate.py
    test_client_mock.py
    test_graph_phase_a.py
```

### 7.1 C++ knowledge docs (`docs/cpp/`)

Curated agent-facing slices of the AlgoCraft C++ surface (not a dump of `AlgoCraft/Notes/`).

| Rule | Detail |
|------|--------|
| Index always | Load `docs/cpp/INDEX.md` into relevant nodes (design_code, compile_loop, research). |
| Selective fetch | From the index table, load **at most 2–3** other doc paths per turn. |
| Catalog folders | `strategies/`, `indicators/`, `routing/` each have their own `INDEX.md` (path + one-line); drill into one body file after picking from that folder index. |
| Codegen default | Prefer `codegen_shape.md` + `strategy_interface.md`; add sizing/clock/indicators as needed. |
| Loader | Implement `docs/loader.py` (or in `llm/prompts.py`): parse index → resolve paths → read files. Optional LLM tool `fetch_doc(path)`. |
| Source of truth | Human architecture stays in AlgoCraft `Notes/`; keep `docs/cpp/` concise and sync when Strategy/API/registry lists change. |

### `pyproject.toml` deps (minimum)

```toml
[project]
name = "algocraft-agent"
requires-python = ">=3.11"
dependencies = [
  "fastapi>=0.115",
  "uvicorn[standard]>=0.32",
  "httpx>=0.27",
  "pydantic>=2.8",
  "pydantic-settings>=2.5",
  "langgraph>=0.2",
  "langchain-core>=0.3",
  "langchain-openai>=0.2",
  "langchain-anthropic>=0.2",
  "sse-starlette>=2.1",
  "python-dotenv>=1.0",
]

[project.optional-dependencies]
dev = ["pytest>=8", "pytest-asyncio>=0.24", "ruff>=0.6"]
```

---

## 8. Config (`.env.example`)

```bash
AGENT_HOST=127.0.0.1
AGENT_PORT=8100

ALGOCRAFT_API_URL=http://127.0.0.1:8080
ALGOCRAFT_USER=agent_bot
ALGOCRAFT_PASS=changeme

LLM_PROVIDER=openai
LLM_MODEL=gpt-4.1-mini
OPENAI_API_KEY=
# ANTHROPIC_API_KEY=
# OLLAMA_BASE_URL=http://127.0.0.1:11434

AGENT_MAX_ITERATIONS=3
AGENT_MAX_COMPILE_ATTEMPTS=5
AGENT_MIN_FILLS=2
AGENT_MAX_FILLS=5000
AGENT_SOFT_MIN_PNL_PAISE=1
AGENT_HTTP_TIMEOUT_SEC=120
AGENT_HTTP_LONG_TIMEOUT_SEC=600
AGENT_COMPILE_TIMEOUT_SEC=180
```

---

## 9. `AlgocraftClient` requirements

```python
class AlgocraftClient:
    def __init__(self, base_url: str, jwt: str | None = None, ...): ...
    def with_jwt(self, jwt: str) -> Self: ...

    async def login(self, user: str, password: str) -> str: ...
    async def list_strategies(self) -> list[str]: ...
    async def list_routers(self) -> list[str]: ...
    async def search_instruments(self, q: str, limit: int = 20) -> list[dict]: ...
    async def ensure_market_data(self, body: dict) -> dict: ...
    async def create_workbook(self, name: str, capital_paise: int) -> int: ...
    async def start_backtest(self, wid: int, *, ticker, strategy, from_ns, to_ns, capital_paise, **kw) -> dict: ...
    async def start_run(self, wid: int, *, router, capital_paise, anchor_date: str, **kw) -> dict: ...
    async def run_events(self, wid: int, rid: int, include: str = "routing,fill") -> list: ...

    async def agent_compile(self, name: str, hpp: str, cpp: str, class_name: str = "") -> dict: ...
    async def agent_promote(self, name: str) -> dict: ...
    async def agent_activate(self, name: str, enabled: bool = True) -> dict: ...
    async def agent_catalog(self) -> list[dict]: ...
```

Raise `AlgocraftApiError(status, body)` on non-2xx. Never log JWT.

---

## 10. Security checklist

1. Allow-listed tools only — no arbitrary URL/path from LLM  
2. Strategy `name` validated client-side same regex as C++  
3. Human confirm before promote/activate  
4. Rate-limit chat (e.g. 20 req/min/session)  
5. Secrets in env only  
6. Phase A must not call promote/activate  

---

## 11. Acceptance tests

### Unit
- `timeutil`: known IST day → nanos round-trip sanity  
- `evaluate`: fills/pnl → pass/weak/fail  
- Graph with **mocked** client: backtest intent ends in `respond` with metrics  

### Integration (optional, needs serve)
1. Login / forward JWT  
2. `list_strategies` non-empty  
3. Backtest `hammer_reversal` on one ticker short window  
4. Hist `runs/start` with `live_run_testing_router` or `top15_week_router` + past `anchor_date`  
5. Compile a tiny valid strategy (can copy stripped `live_run_testing` renamed) → `ok=true`  
6. Promote → catalog `enabled=false` → activate → `enabled=true`  

### Manual UX
```
User: Backtest hammer_reversal on ONGC for last 5 sessions with 1L
→ card with fills, pnl_paise, verdict
```

---

## 12. Implementation order (checklist)

- [ ] 1. Repo + `pyproject.toml` + `.env.example` + README  
- [ ] 2. `config.py` + `/healthz` (ping C++ `/strategies` or `/auth/me`)  
- [ ] 3. `AlgocraftClient` + `timeutil` + unit tests  
- [ ] 4. `SessionStore` + `POST /v1/sessions`  
- [ ] 5. LangGraph Phase A nodes + mock tests  
- [ ] 6. Wire real backtest + evaluate  
- [ ] 7. Wire hist `start_run` + events  
- [ ] 8. `POST /v1/chat` + SSE stream  
- [ ] 9. Phase B `design_code` + `compile_loop`  
- [ ] 10. Phase C confirm + promote + activate  
- [ ] 11. (Separate) AlgoCraft-UI chat panel  

---

## 13. Decision summary (locked)

| Topic | Decision |
|-------|----------|
| Framework | FastAPI + LangGraph StateGraph |
| Agent port | **8100** |
| C++ port | **8080** |
| Auth | Forward user JWT to C++ |
| Phase A | Existing strategies/routers only |
| Phase B/C C++ | `/agent/strategies/compile\|promote\|activate` + `GET .../catalog` (**done in AlgoCraft**) |
| Promote | Catalog `enabled=0` |
| Activate | Catalog `enabled=1`; rebuild+restart to load code |
| Eval | Soft pass/weak/fail |
| Hist default | Past `anchor_date` |
| Long timeout | 600s runs/backtests |
| UI | Later |

---

## 14. Sibling references

| Path | Why |
|------|-----|
| `AlgoCraft/src/api/agent_routes.cpp` | Exact agent HTTP handlers |
| `AlgoCraft/src/strategies/strategy_compiler.cpp` | Sandbox + promote file rules |
| `AlgoCraft/migrations/schema_009.sql` | `strategy_catalog` |
| `AlgoCraft/src/strategies/live_run_testing.*` | Minimal strategy template |
| `AlgoCraft/src/api/workbook_routes.cpp` | backtest/run body fields |

---

*When starting: create `AlgoCraft-Agent`, copy this file to `ARCHITECTURE.md`, implement checklist §12 in order.*
