# AlgoCraft-Agent — Architecture

**Repo (to create):** `AlgoCraft-Agent` — Python FastAPI + LangGraph  
**Sibling repos:** `AlgoCraft` (C++ engine), `AlgoCraft-UI` (React), `Virtual_Websockets` (live tape mock)  
**Status:** design only — no code yet. This file becomes `ARCHITECTURE.md` in the new repo.

---

## 1. Purpose

Give the user a **chat surface** that can:

1. Understand a trading idea in plain language  
2. Research tickers / existing strategies via the **live C++ HTTP API**  
3. Propose or refine a strategy idea  
4. Run **real** backtests / hist routing through AlgoCraft (same APIs the UI uses)  
5. Iterate until metrics look acceptable  
6. Hand results to the human for approval  

**Non-goals (v1):** auto-activate live trading, write unchecked C++ into production, bypass human review.

The C++ engine stays the source of truth for capital, bars, fills, risk. The Python service is an **orchestrator + LLM brain**, not a second trading engine.

---

## 2. Placement in the platform

```
AlgoCraft-UI (React)
        │
        ├── JWT HTTP ──► AlgoCraft (:8080)     backtests, runs, portfolio, strategies
        │
        └── JWT/session ─► AlgoCraft-Agent (:8100)   chat + LangGraph
                                    │
                                    └── tool calls ──► AlgoCraft (:8080)
                                              (service user or forwarded user JWT)
```

| Rule | Detail |
|------|--------|
| Two backends, one UI | UI talks to C++ and Python independently |
| No hot-path in Python | No bars, rings, risk, or order routing in Python |
| Same APIs as frontend | Agent tools wrap existing Crow routes only |
| Auth | Agent authenticates to C++ as a dedicated user (`agent_bot`) or proxies the user’s Bearer token |

---

## 3. Delivery phases (Python repo)

Build the agent **in slices**. Do not wait for compile/dlopen.

### Phase A — Chat + tools (ship first)

- FastAPI: `POST /v1/chat`, `GET /v1/sessions/{id}`, SSE stream  
- LangGraph: Research → Propose → Backtest → Evaluate → Respond  
- Tools against **existing** C++ APIs only:
  - `POST /auth/login`
  - `GET /strategies`, `GET /routing-algos`
  - `GET /instruments?q=`
  - `POST /workbooks`, `POST .../backtests/start`, `GET .../backtests/{id}`
  - `POST .../runs/start` (`top15_week_router` / `default_router` + `anchor_date`)
  - `GET .../runs/{id}/events?include=routing,fill`
- Strategy “design” = structured JSON plan + pick among **registered** strategies (no codegen yet)
- Human sees proposal + metrics in chat; approval is “looks good / reject” only

### Phase B — Codegen to staging (later)

- Strategy Designer emits `.hpp` / `.cpp` matching AlgoCraft `Strategy` interface  
- Needs new C++: `POST /strategies/compile` (sandbox) — **not built yet**  
- Compile errors loop back into LangGraph until green  
- Still no live activation

### Phase C — Promote + load (later)

- Finalizer stages registration line / CMake or `.so` plugin  
- Human `PATCH .../activate`  
- Option A: rebuild + `scripts/serve --restart`  
- Option B (future): `dlopen` plugin loader  

---

## 4. LangGraph design

### 4.1 Graph (Phase A)

```
                 ┌─────────────┐
                 │  orchestrator│  (entry: user message + session state)
                 └──────┬──────┘
                        ▼
                 ┌─────────────┐
                 │  research   │  instruments, OHLC ensure, list strategies
                 └──────┬──────┘
                        ▼
                 ┌─────────────┐
                 │  propose    │  pick strategy(+ticker) or structured plan
                 └──────┬──────┘
                        ▼
                 ┌─────────────┐
                 │  execute    │  backtest and/or hist run (router)
                 └──────┬──────┘
                        ▼
                 ┌─────────────┐
            ┌───►│  evaluate   │─── fail / iterate ──► propose (max N)
            │    └──────┬──────┘
            │           │ pass
            │           ▼
            │    ┌─────────────┐
            └───│  respond    │  stream summary + metrics to UI
                 └─────────────┘
```

Optional later nodes: `compile`, `finalize` (Phases B/C).

### 4.2 Shared state (TypedDict / Pydantic)

```python
class AgentState(TypedDict, total=False):
    session_id: str
    user_id: str
    workbook_id: int | None
    messages: list[dict]          # chat history
    intent: str                   # research | backtest | route | explain
    tickers: list[str]
    strategy_candidates: list[str]
    chosen: dict                  # {strategy, ticker, window, capital_paise}
    cpp_results: dict             # raw API payloads
    metrics: dict                 # pnl, fills, sharpe-proxy, etc.
    eval_verdict: str             # pass | fail | need_human
    feedback: str                 # for next propose iteration
    iteration: int
    max_iterations: int           # hard cap (e.g. 3)
    error: str | None
```

### 4.3 Node contracts

| Node | Input | Tools / LLM | Output |
|------|--------|-------------|--------|
| **research** | user text | instruments search, strategies list, optional ensure OHLCV | `tickers`, short research notes |
| **propose** | research + feedback | LLM constrained to registered strategy names + params | `chosen` |
| **execute** | `chosen` | `backtests/start` and/or `runs/start` | `cpp_results` |
| **evaluate** | `cpp_results` | thresholds (configurable) | `eval_verdict`, `feedback`, `metrics` |
| **respond** | state | LLM formats answer | assistant message (+ optional structured card JSON) |

### 4.4 Evaluate thresholds (defaults, env-overridable)

```
min_pnl_paise:     > 0          # Phase A: keep simple
max_fills:         200          # fee sanity
min_fills:         2
max_iterations:    3
```

Later (when C++ returns richer stats): Sharpe, max DD, win rate.

---

## 5. FastAPI surface

| Method | Path | Role |
|--------|------|------|
| `POST` | `/v1/auth/login` | optional; or accept UI-forwarded C++ JWT |
| `POST` | `/v1/sessions` | create agent session `{ workbook_id? }` |
| `POST` | `/v1/chat` | `{ session_id, message }` → start/continue graph |
| `GET`  | `/v1/chat/{session_id}/stream` | SSE: tokens + tool events + final card |
| `GET`  | `/v1/sessions/{id}` | history + last metrics |
| `GET`  | `/healthz` | liveness |

**Streaming:** SSE preferred for UI simplicity (`event: token | tool | result | error`).

---

## 6. Tools → AlgoCraft C++ (Phase A, real routes)

All tools are thin HTTP clients (`httpx`). Base URL: `ALGOCRAFT_API_URL` (default `http://127.0.0.1:8080`).

| Tool name | C++ call | Notes |
|-----------|----------|-------|
| `login` | `POST /auth/login` | service account or user creds |
| `list_strategies` | `GET /strategies` | constrain propose node |
| `list_routers` | `GET /routing-algos` | e.g. `top15_week_router` |
| `search_instruments` | `GET /instruments?q=` | ticker resolve |
| `ensure_market_data` | `POST /market-data/ensure` | before long windows |
| `create_workbook` | `POST /workbooks` | if session has none |
| `start_backtest` | `POST /workbooks/{id}/backtests/start` | strategy × ticker × range |
| `get_backtest` | `GET /workbooks/{id}/backtests/{bt}` | result payload |
| `start_run` | `POST /workbooks/{id}/runs/start` | `{ router, anchor_date, capital_paise }` |
| `run_events` | `GET .../runs/{id}/events?include=routing,fill` | top-15 CREATE list |

**Do not invent** `/strategies/compile` in Phase A tools.

---

## 7. Proposed Python repo layout

```
AlgoCraft-Agent/
  ARCHITECTURE.md          ← this document
  README.md
  pyproject.toml           # fastapi, uvicorn, langgraph, langchain-*, httpx, pydantic
  .env.example
  app/
    main.py                # FastAPI app
    config.py              # settings from env
    api/
      chat.py
      sessions.py
      health.py
    graph/
      state.py
      graph.py             # StateGraph wire-up
      nodes/
        research.py
        propose.py
        execute.py
        evaluate.py
        respond.py
    tools/
      algocraft_client.py  # httpx wrapper + JWT
      market.py
      backtest.py
      routing.py
    llm/
      prompts.py
      provider.py          # OpenAI / Anthropic / local via env
    models/                # pydantic request/response
  tests/
    test_graph_unit.py     # mocked C++ client
    test_api_smoke.py
```

Keep it thin — no duplicate domain models of bars/fills beyond API DTOs.

---

## 8. Config (`.env`)

```bash
# AlgoCraft-Agent
AGENT_HOST=127.0.0.1
AGENT_PORT=8100

# C++ engine
ALGOCRAFT_API_URL=http://127.0.0.1:8080
ALGOCRAFT_USER=agent_bot
ALGOCRAFT_PASS=...

# LLM
LLM_PROVIDER=openai          # openai | anthropic | ollama
LLM_MODEL=gpt-4.1-mini
OPENAI_API_KEY=
# ANTHROPIC_API_KEY=
# OLLAMA_BASE_URL=http://127.0.0.1:11434

# Graph limits
AGENT_MAX_ITERATIONS=3
AGENT_MIN_PNL_PAISE=1
AGENT_MAX_FILLS=200
```

Resource caps (laptop-safe): single uvicorn worker, max concurrent graphs = 1–2, tool timeouts ≤ 120s for hist runs.

---

## 9. UX (UI later)

```
User: "Test top15 week router on 2026-09-25 with 10L"

Agent:
  research → list routers, confirm top15_week_router
  propose  → { router: top15_week_router, anchor_date: 2026-09-25, capital: 10L }
  execute  → POST /runs/start
  evaluate → selected=15, fills=…, returned_paise=…
  respond  → table of CREATE containers + verdict
```

```
User: "Backtest hammer_reversal on ONGC for last week"

Agent → start_backtest → metrics card → pass/fail vs thresholds
```

Chatbot is **global** (not locked to one workbook); workbook is created or selected per session.

---

## 10. C++ prerequisites

### Already enough for Phase A

| Need | Status |
|------|--------|
| Auth + JWT | Done |
| Strategies / routers list | Done |
| Backtests start + get | Done |
| Runs start (hist / live anchor) | Done |
| Routing events | Done |
| Instruments + ensure | Done |

### Required before Phase B/C

| Need | Status |
|------|--------|
| `POST /strategies/compile` (sandbox) | Not built |
| Agent strategy provenance columns | Not built |
| Activate / load plugin endpoints | Not built |
| Docker compile sandbox | Not built |

Phase A must not block on these.

---

## 11. Security

1. **No shell from LLM** — tools are allow-listed functions only  
2. **No direct FS / git write** in Phase A  
3. **Iteration + rate limits** on graph and on C++ calls  
4. **Human gate** before any future promote-to-live  
5. **Secrets** only in env; never log JWT or API keys  
6. Phase B compile only in isolated sandbox (when built)

---

## 12. Relation to routing / Virtual_Websockets

| Piece | Role for agent |
|-------|----------------|
| `top15_week_router` | Primary “portfolio day” tool via `runs/start` |
| Hist `anchor_date` | Default agent testing path (any past NSE day) |
| Live + `ALGOCRAFT_LIVE_FEED=virtual` | Optional later tool; Sunday/holiday blocked by NSE calendar |
| Virtual_Websockets | Independent process; agent does not embed it |

---

## 13. Implementation order (when we create the repo)

1. Skeleton: FastAPI + `/healthz` + config  
2. `AlgocraftClient` + one smoke tool (`list_strategies`)  
3. LangGraph Phase A graph with mocked tools in unit tests  
4. Wire real backtest tool + evaluate thresholds  
5. Wire `runs/start` + routing events  
6. SSE chat endpoint  
7. UI panel (AlgoCraft-UI) — separate step  

---

## 14. Decision summary

| Topic | Decision |
|-------|----------|
| Framework | **FastAPI** + **LangGraph** StateGraph |
| Port | **8100** (C++ stays 8080) |
| v1 strategy work | Select/run **existing** strategies & routers — no C++ codegen yet |
| Iteration | Max 3 propose↔execute loops |
| Approval | Human in the loop for any permanent promote (Phase C) |
| C++ changes for Phase A | **None** |

---

*Next step when you say go: create `AlgoCraft-Agent` repo and copy this file to `ARCHITECTURE.md`, then implement Phase A skeleton.*
