# SPSC / threading roadmap

ARCHITECTURE.md describes the 5-thread / 8-ring design. This note tracks **implementation steps**.

## Done

### Logging (prep)
- `LogHub` multi-producer queue → spdlog (`AC_LOG_*`, CLI levels, daily files).
- See `Notes/LOGGING.md`.

### Step 1 — Dual SQLite + PersistenceService (T2) ✅
- `SqliteDatabase::open_readonly()` for T4 SELECTs.
- `EngineCache`: `db_write` (migrate + mutates) + `db_read` (API / coverage reads).
- `PersistenceService` (`thread2_persistence`): sole writer; drains `LogHub` (no separate `log_drain` on serve/run/backtest).
- Mutates enqueue via `run_sync` / `try_async`: auth register, workbook CRUD/capital, soft-deletes, `persist_run`, backtest insert, coverage upsert.
- API repos bind to `db_read`.
- **Note:** job ingress is still a **mutex queue** (not Architecture’s three Persist_* SPSC rings). Isolation goal met; typed rings deferred — see Open below.

### Step 2 — Live MarketDataRing ✅
- `LiveRunService::ActiveRun` owns `SpscRing<BarEvent, 4096>`.
- Feed / WS (or test) thread: `MinuteBarBuilder` → `try_push` only (never runs strategy).
- ActiveRun loop (T0): `try_pop` → scheduler / `containers.on_bar` / `router->on_bar`.

### Step 2b — Live OrderOut + FillIn ✅
- `SpscRing<LiveOrderRequest>` (T0 → T3) + `SpscRing<FillEvent>` (T3 → T0).
- `RingExecutionVenue`: `submit` only `try_push`es (returns nullopt); hist/backtest still use sync `SimulatedExchange`.
- T3 thread runs `SimulatedExchange` and pushes fills; T0 drains FillIn → `ContainerManager::on_fill`.
- Shutdown: flatten → join T3 → drain fills → persist (no second flatten).
- Drop counters: `orders_dropped`, `fills_dropped`, `fills_processed`.

### Step 3 — Command + Routing rings ✅
- `LiveCommand` / `RoutingSignal` in `live_control.hpp`.
- **CommandRing** (control/API → T0): `stop()` enqueues KillSwitch + StopRun; also KillContainer.
- **RoutingRing** (T0 → T1): bars + session start/end; T1 owns `router->on_bar` / session hooks.
- Scheduler session edges still apply risk/containers on T0 (same thread); router session forwarded on RoutingRing.
- Metrics: `commands_processed`, `routing_signals`, drop counters.

### Step 4 — Hot-path isolation ✅
- T0/T1 bar/fill/command/routing loops stay off SQLite/RocksDB.
- `settle_and_persist`: in-memory finalize + hub snapshot on T0; SQLite via `persist->try_async` (fallback `run_sync` if queue full). No `workbooks.find` on T0.
- `start()` `set_available` goes through T2 when `persist` is set.
- Live UI continues via `StatusSseHub` snapshots; portfolio JSON includes drop counters.
- OrderOut full → `AC_LOG_WARN`.

## Open

### Step 5 — Persist_* typed SPSC (optional / next)
Blockers vs current mutex queue (nothing fundamental — design work):
1. **Payloads are `std::function`** — not POD; need typed `PersistEvent` variants + rewrite call sites.
2. **API needs sync replies** (`run_sync` + promise) — pure one-way rings need a reply path or async-only API.
3. **Many producers** (HTTP workers, live T0, backtest) — Architecture’s three rings assume one producer each (T0/T1/T3); API still needs MPSC or a funnel thread.
4. **Correctness already OK** — dual SQLite + T2 sole writer; rings are latency/purity, not isolation.

## Gate each step
Build (`-j1`/`-j2`), unit tests, short serve smoke. Do not advance with a red gate.
