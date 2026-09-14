# AI Agent System — Strategy Research & Auto-Generation

---

## 1. Overview

A multi-agent AI system added to the platform **after all major phases are complete**. It lives alongside the main app — same UI, separate Python backend — and gives users the ability to describe a trading idea in plain language, let AI agents research the stock, design a strategy, backtest it using the existing C++ infrastructure, iterate until results are good, and finally integrate the approved strategy permanently into the C++ codebase.

**The main C++ app requires zero structural changes to support this.** It is already extensible by design.

---

## 2. What the User Experience Looks Like

```
User opens the chatbot (one global chatbot, accessible from any workbook)

User types:
  "Find a momentum strategy for RELIANCE that works well on gap-up opens"

Agent system:
  → researches RELIANCE historical data using the existing C++ API
  → designs a C++ strategy class
  → compiles and backtests it (using existing /backtests API)
  → evaluates results — iterates if not satisfied
  → once satisfied: submits for user review

User reviews:
  → sees backtest results, equity curve, Sharpe, drawdown
  → approves or rejects

On approval:
  → strategy is added to the C++ codebase permanently
  → appears in the UI strategy dropdown for all workbooks
  → routing algo can use it immediately
```

---

## 3. System Architecture

### 3.1 Two Backends, One UI

```
React / TypeScript Frontend
        |
        |— HTTP —> C++ Backend    (existing – runs, backtests, paper trades)
        |
        └— HTTP —> Python Backend (new – AI agent orchestration)
```

The UI calls both backends independently. No changes to the C++ backend's API. The Python backend is a separate process/service.

### 3.2 Python Backend (LangGraph)

```
FastAPI or Flask HTTP server
  → exposes chat endpoint: POST /chat { workbook_id, message, stock }
  → receives streaming responses back to UI
  → internally runs the LangGraph multi-agent graph
```

### 3.3 LangGraph Agent Graph

```
                    Orchestrator
                (LangGraph state machine)
                        |         ^
              coordinates|         |results / decisions
                        v         |
     ┌──────────────────────────────────────────┐
     ▼                                            
┌─────────┐   ┌──────────┐   ┌─────────┐   ┌───────────┐
│Research │──>│Strategy  │──>│Compiler │──>│Evaluator  │
│Agent    │   │Designer  │   │Agent    │   │Agent      │
└─────────┘   └──────────┘   └─────────┘   └───────────┘
     ^             ^               |          |        |
     |     iterate |  if compile   |     not  |        |satisfied
     |             |  or backtest  |satisfied |        |
     |             |  unsatisfactory           |        v
     |             └───────────────┘           |   ┌───────────┐
     └───────────────── C++ REST API ──────────┘   │ Finalizer │
                                                     │ Agent     │
                                              loop back  └───────────┘
                                              to designer      |
                                                                v
                                                        human review
                                                        → approve/reject
```

---

## 4. Each Agent's Responsibility

### Agent 1 — Research Agent
```
Input:  stock ticker (e.g. "RELIANCE") + user's prompt
Tools:
  → GET /workbooks/{wid}/containers/{cid}/chart  ← fetch historical bar data
  → GET /strategies                              ← see what strategies already exist
  → calls LLM to identify patterns, seasonality, gap behaviours, volatility regime

Output: structured market analysis report
  {
    ticker: "RELIANCE",
    observations: ["strong gap-up open momentum on high volume days", ...],
    suggested_indicators: ["EMA(9)", "EMA(21)", "ATR(14)", "volume_ratio"],
    suggested_entry_conditions: [...],
    suggested_exit_conditions: [...]
  }
```

### Agent 2 — Strategy Designer Agent
```
Input:  research report + user prompt + previous iteration's compiler/backtest errors
Tools:
  → LLM with our Strategy interface as system context (see Section 6)
  → Knows: Strategy base class signature, IndicatorLibrary API,
           BarEvent fields, OrderIntent structure, StrategyMetadata fields

Output: valid C++ strategy code
  → MomentumGapStrategy.hpp
  → MomentumGapStrategy.cpp
  → follows our Strategy interface exactly
  → uses only indicators available in IndicatorLibrary
```

### Agent 3 — Compiler Agent
```
Input:  generated C++ files
Action:
  → writes files to a staging directory (not the main codebase yet)
  → calls: POST /strategies/compile { source_code }
     ← C++ server compiles in sandbox, returns: { success, errors }
  → if errors: sends error messages back to Strategy Designer Agent to fix
  → loops until compilation succeeds

Output: compiled artifact ready for backtesting
```

### Agent 4 — Backtest Agent
```
Input:  compiled strategy (loaded into C++ server)
Tools:
  → POST /workbooks/{wid}/backtests/start
      { stock: "RELIANCE", strategy: "momentum_gap_v1",
        capital: 1_00_000, from: "2020-01-01", to: "2024-01-01" }
  → GET /workbooks/{wid}/backtests/{id}          ← polls for completion
  → GET /workbooks/{wid}/containers/{cid}/chart  ← fetches signal overlay

Output: backtest result { total_return, sharpe, max_drawdown, win_rate, trades }
```

### Agent 5 — Evaluator Agent
```
Input:  backtest results
Logic:
  → checks configurable thresholds:
      min_sharpe:        1.2
      max_drawdown_pct:  20%
      min_win_rate:      45%
      min_trades:        50   (enough data to be statistically valid)
  → if all pass: SATISFIED → forward to Finalizer
  → if any fail: NOT_SATISFIED → send feedback to Strategy Designer to revise
      e.g. "Drawdown too high (28%). Tighten stop-loss or reduce position size."

Output: SATISFIED | NOT_SATISFIED + specific feedback
```

### Agent 6 — Finalizer Agent
```
Input:  finalized strategy code + backtest results
Action (Option A — recompile+restart, initial implementation):
  1. Write MomentumGapStrategy.hpp and .cpp to strategies/ directory in C++ repo
  2. Append one line to strategy_registrations.cpp:
       registry.register<MomentumGapStrategy>("momentum_gap_v1");
  3. Append one line to CMakeLists.txt (add new .cpp to build target)
  4. Run: cmake --build build/
  5. Insert row into strategies DB table:
       { name: "momentum_gap_v1", source: AGENT_GENERATED_PENDING,
         version: "1.0.0", trading_mode: MIS, description: "...",
         backtest_result_id: <id>, created_by_agent: true }
  6. Notify user: "Strategy ready for review"

On user approval:
  → PATCH /admin/strategies/{id}/activate
  → source changes: AGENT_GENERATED_PENDING → AGENT_GENERATED_ACTIVE
  → restart C++ server (Option A) or dlopen (Option B future)
  → strategy now appears in UI dropdown for all workbooks
```

---

## 5. Integration Points — What Needs to Change in C++ (Future Phase)

The main C++ app needs **three small additions** when this phase is implemented. All are additive — nothing existing changes.

### 5.1 `POST /strategies/compile` endpoint (new)
```
Receives:  C++ strategy source code
Does:
  → writes to a sandboxed temp directory
  → runs compiler against it (with our headers available)
  → returns: { success: true } or { success: false, errors: "..." }
  → on success (Option B): compiles as .so, loads via dlopen
  → on success (Option A): source staged for inclusion in next build
Never:     writes directly to production codebase without human approval
```

### 5.2 `strategies` table gets two new columns
```sql
ALTER TABLE strategies ADD COLUMN source VARCHAR(32) DEFAULT 'BUILT_IN';
  -- values: BUILT_IN | AGENT_GENERATED_PENDING | AGENT_GENERATED_ACTIVE

ALTER TABLE strategies ADD COLUMN created_by_agent BOOLEAN DEFAULT FALSE;
ALTER TABLE strategies ADD COLUMN backtest_result_id UUID REFERENCES backtests(id);
ALTER TABLE strategies ADD COLUMN agent_session_id VARCHAR(64);
```

`GET /strategies` returns only `BUILT_IN` and `AGENT_GENERATED_ACTIVE` strategies by default.
Admin/owner sees `PENDING` ones too.

### 5.3 `strategy_registrations.cpp` — dedicated registration file
```cpp
// strategy_registrations.cpp
// This is the ONLY file the agent ever touches for registration.
// It is separate from StrategyRegistry.cpp (which never changes).

void registerAllStrategies(StrategyRegistry& registry) {
    // Built-in strategies (manually written, always present):
    registry.register<EMACrossover>("ema_crossover_v1");
    registry.register<VWAPReversion>("vwap_reversion_v1");
    registry.register<OpeningRangeBreakout>("orb_v1");

    // Agent-generated strategies (appended by Finalizer Agent, one line each):
    // registry.register<MomentumGapStrategy>("momentum_gap_v1");
}
```

The agent only appends one line. It never modifies `StrategyRegistry.cpp` or any other core file.

---

## 6. LLM Context — What the Strategy Designer Agent Gets Told

The LLM must know our exact interface to write valid code. This is injected as system context:

```
You are a C++ trading strategy developer. Every strategy you write must:

1. Inherit from Strategy base class:
   class MyStrategy : public Strategy {
   public:
     void configure(StrategyConfig config, IndicatorLibrary& lib) override;
     std::vector<OrderIntent> onBar(const BarEvent& bar, const PortfolioView& pv) override;
     void onFill(const FillEvent& fill) override;
     void onOrderUpdate(const OrderEvent& event) override;
     bool shouldExit() override;
     StrategyMetadata metadata() const override;
   };

2. Declare metadata():
   StrategyMetadata metadata() const override {
     return {
       .name = "my_strategy_v1",
       .version = "1.0.0",
       .trading_mode = TradingMode::MIS,        // or CNC
       .required_resolution = BarResolution::ONE_MIN,
       .required_indicators = { "EMA", "RSI" }
     };
   }

3. Request indicators in configure() only (not in onBar):
   void configure(StrategyConfig config, IndicatorLibrary& lib) override {
     ema_fast_ = &lib.get<EMA>(config.symbol, {.period = 9});
     ema_slow_ = &lib.get<EMA>(config.symbol, {.period = 21});
     rsi_      = &lib.get<RSI>(config.symbol, {.period = 14});
   }

4. Use indicators in onBar() (read-only, never modify):
   if (ema_fast_->value() > ema_slow_->value() && rsi_->value() < 70) { ... }

5. Return OrderIntents, never submit orders directly:
   return { OrderIntent { .side = Side::BUY, .qty = 100, .type = OrderType::MARKET } };

6. Never: use std::cout, std::mutex, new/delete, file I/O, or global state.
```

---

## 7. Option A vs Option B — Strategy Loading

### Option A (initial implementation — recompile + restart)
```
Agent finalizes strategy
  → writes .hpp/.cpp to repo
  → appends to strategy_registrations.cpp
  → appends to CMakeLists.txt
  → cmake --build
  → C++ server restarts
```

Downside: server restart required — any live trades must be stopped first
Upside:   simple to implement

### Option B (future upgrade — shared library, no restart)
```
Agent finalizes strategy
  → writes .hpp/.cpp to plugins/ directory
  → adds extern "C" factory function to .cpp:
      extern "C" Strategy* create_strategy() { return new MomentumGapStrategy(); }
      extern "C" const char* strategy_name() { return "momentum_gap_v1"; }
  → compiles as shared library:
      g++ -shared -fPIC -o plugins/momentum_gap_v1.so momentum_gap.cpp
  → calls: POST /strategies/load { plugin: "momentum_gap_v1" }
  → C++ server: dlopen("plugins/momentum_gap_v1.so")
        → extracts factory function
        → registry.register("momentum_gap_v1", factory_fn)
        → strategy immediately available — zero restart
```

**Switching A → B:** The strategy class code is **100% identical** between A and B. Only the wiring changes (one extra `extern "C"` block, no CMakeLists.txt edit, no restart). Switching when ready is a small isolated change.

---

## 8. Security Considerations

The agent is writing C++ code that gets compiled and potentially run on the same machine as the trading engine. Non-negotiable safeguards:

```
1. Compilation sandbox
   → agent-generated code compiled in an isolated Docker container
   → container has no network access, no file system access beyond the staging dir
   → compiled .so or binary is then moved to the main system only after human approval

2. Human review gate is mandatory
   → strategies never auto-activate into live trading
   → user must explicitly approve before a strategy goes from PENDING → ACTIVE
   → backtest results are always shown during review

3. Compilation error feedback is text-only
   → compiler stderr returned to agent as plain text
   → no execution of any compiled code before approval

4. Agent-generated strategies are tagged forever
   → created_by_agent = true, agent_session_id stored in DB
   → visible in audit logs, UI labels them as AI-generated

5. Rate limits on the compile endpoint
   → prevent runaway agent loops from hammering the build system
   → max N compile attempts per session, configurable

6. No direct filesystem writes by the agent
   → all file writes go through the C++ backend's compile endpoint
   → agent never has direct SSH or filesystem access to the server
```

---

## 9. What Is Already Compatible in the Current Architecture

No changes needed to the current app to keep this door open:

| What the agent needs | Current status |
|---|---|
| `POST /workbooks/{wid}/backtests/start` | Already designed |
| `GET /workbooks/{wid}/backtests/{id}` | Already designed |
| `GET /strategies` | Already designed |
| `StrategyRegistry` for runtime registration | Already designed |
| `strategies` DB table | Already designed |
| `Strategy` abstract interface | Already designed — stable contract for LLM context |
| `BarEvent`, `OrderIntent`, `PortfolioView` types | Already defined |
| Workbook-scoped API (agent works within a workbook) | Already designed |

---

## 10. What Gets Added in the AI Agent Phase (future)

All additive. Zero changes to existing code:

```
C++ additions:
  strategy_registrations.cpp           ← new dedicated registration file
  POST /strategies/compile             ← new endpoint (sandboxed compilation)
  POST /strategies/load                ← new endpoint (Option B: dlopen)
  PATCH /admin/strategies/{id}/activate ← new endpoint (human approval)
  StrategyPluginLoader                 ← new class (Option B: wraps dlopen)
  strategies table: 4 new columns      ← source, created_by_agent, backtest_result_id, agent_session_id

Python additions (entirely new service):
  FastAPI server
  LangGraph graph (6 agents)
  LLM integration (OpenAI / Anthropic / local model)
  Compilation sandbox (Docker)
  Tool definitions (C++ API calls as LangGraph tools)

UI additions:
  Chatbot panel (global, not workbook-specific)
  Strategy review screen (shows agent-generated strategy + backtest results)
  "AI-generated" badge on strategies created by agent
```

---

## 11. Build Note

This phase is implemented **after Phase 11 (Production Hardening)** of the main platform.

Prerequisites before starting AI Agent phase:
- All main phases (0–11) complete and stable
- C++ REST API fully tested and documented
- Strategy interface finalized and not expected to change
- At least 3+ built-in strategies working in production (gives LLM good examples)
