# C++ Algorithmic Trading Platform — Implementation Plan

---

## Purpose of this document

ARCHITECTURE.md defines **what** we are building and **why**. This document defines **how** we build it, **in what order**, and **what rules are non-negotiable from day one** to ensure we never need a structural rewrite when we go to Level 2 (HFT-grade) optimization.

---

## Part 1: Non-Negotiable Foundations (must be in place before any trading logic)

These 4 rules are not optional future optimizations. They are constraints baked into the project skeleton before the first strategy or indicator is written. Violating any of them now means a painful structural rewrite later.

---

### Foundation 1: Lock-Free Ring Buffer as the Event Bus

**Rule:** No component sends events to another component via a locked queue, function call across threads, or shared mutable state protected by `std::mutex`. All inter-component communication happens through a lock-free ring buffer.

**Pattern:** LMAX Disruptor

**How to implement:**

The ring buffer is a fixed-size circular array of pre-allocated event slots. Each slot has a sequence number (atomic integer). Producers and consumers coordinate using only atomic CPU instructions — no OS locks, no thread sleep/wake.

```
Ring buffer layout (allocated once at startup):
  Slot 0:  { sequence: 0, event: BarEvent }
  Slot 1:  { sequence: 0, event: BarEvent }
  ...
  Slot N:  { sequence: 0, event: BarEvent }
  (wraps back to Slot 0)

Producer writes to slot K:
  → atomic read of slot K sequence
  → if sequence matches expected → write event data → atomic increment sequence
  → time cost: ~5 nanoseconds

Consumer reads from slot K:
  → atomic read of slot K sequence
  → if sequence matches expected → read event data → advance cursor
  → time cost: ~5 nanoseconds
  → no waiting, no OS call, no thread suspend
```

**Library to use:** `rigtorp/SPSCQueue` (single producer, single consumer) or implement the Disruptor pattern directly. Do NOT use `std::queue` with `std::mutex` anywhere in the hot path.

**All ring buffers are SPSC (Single Producer, Single Consumer).**

The hot path avoids SPMC entirely: Thread 0 reads market data alone and dispatches to indicators, strategies, and risk engine as sequential function calls on the same thread — not as separate consumer threads. This keeps all hot-path data in one CPU's L1/L2 cache throughout the cycle.

The only multi-producer case is persistence. We handle it with one dedicated SPSC ring per producer (Option A) — zero contention between producers, persistence thread polls all three in round-robin.

```
SPSC rings (hot path — fastest, zero overhead):

1. MarketDataRing:     NSE feed thread ───────► Thread 0 (hot path)
                        (BarEvents, TickEvents)

2. OrderOutRing:       Thread 0 ───────► Thread 3 (execution)
                        (Orders to send to broker)

3. FillInRing:         Thread 3 ───────► Thread 0 (hot path)
                        (Fills, OrderUpdates from broker)

4. RoutingRing:        Thread 0 ───────► Thread 1 (routing algo)
                        (bar signals, container status snapshots)

5. CommandRing:        Thread 1 ───────► Thread 0 (hot path)
                        (create container, kill container commands)

SPSC rings for persistence (one per producer — no contention between producers):

6. PersistenceRing_0:  Thread 0 ───────► Thread 2 (persistence)
                        (fills, order updates, position snapshots)

7. PersistenceRing_1:  Thread 1 ───────► Thread 2 (persistence)
                        (container lifecycle events)

8. PersistenceRing_3:  Thread 3 ───────► Thread 2 (persistence)
                        (broker acknowledgements, raw fill events)

  Thread 2 polls PersistenceRing_0, _1, _3 in round-robin.
  No atomic CAS between producers. Each producer owns its ring exclusively.
```

**What to never do:**
```cpp
// WRONG — locks in hot path
std::mutex mtx;
std::queue<BarEvent> eventQueue;
void onBar(BarEvent e) {
    std::lock_guard<std::mutex> lock(mtx);  // ← latency spike every call
    eventQueue.push(e);
}

// RIGHT — lock-free ring buffer
RingBuffer<BarEvent, 65536> marketDataRing;
void onBar(BarEvent e) {
    marketDataRing.write(e);  // ← ~5 nanoseconds, no OS call
}
```

---

### Foundation 2: Persistence is Always Async — Never in the Hot Path

**What to never do:**
```cpp
// WRONG — synchronous DB write in hot path
void onFill(Fill fill) {
    db.execute("INSERT INTO fills VALUES (...)", fill);  // ← blocks 5-50ms
    strategy.onFill(fill);
}

// RIGHT — async, non-blocking
void onFill(Fill fill) {
    persistenceRing.write(PersistenceEvent{FILL, fill});  // ← ~5ns, non-blocking
    strategy.onFill(fill);
}
// Persistence thread picks it up independently
```

---

### Foundation 3: Zero Heap Allocation in the Hot Path

**Rule:** After the application startup phase is complete and before the first market event is processed, zero calls to `new`, `delete`, `malloc`, or `free` occur on the hot-path thread. All objects used during trading are pre-allocated at startup.

**How to implement:**

At application startup (before market opens):

```
Memory pools to pre-allocate:

OrderPool:
  → allocate 10,000 Order objects in a contiguous block
  → free-list of available slots
  → acquire() returns next available slot in ~2ns
  → release(order) returns it to free-list in ~2ns

BarEventPool:
  → pre-allocated inside the ring buffer slots
  → ring buffer owns the memory, never allocates per-event

FillPool:
  → allocate 10,000 Fill objects

IndicatorStatePool:
  → all EMA, VWAP, RSI, ATR state objects
  → allocated once at startup: 300 symbols × N indicators = fixed count
  → never allocated again during trading

ContainerPool:
  → allocate max expected containers at run start
  → e.g. 300 containers if running 300 stocks

StringInterning:
  → symbol names ("RELIANCE", "INFY") stored in a symbol table at startup
  → hot path uses integer symbol IDs, never std::string
```

**What to never do:**
```cpp
// WRONG — heap allocation in hot path
void onBar(BarEvent bar) {
    auto order = std::make_shared<Order>(...);  // ← heap allocation, unpredictable
    auto result = new StrategyResult(...);       // ← heap allocation
}

// RIGHT — pool allocation
void onBar(BarEvent bar) {
    Order* order = orderPool.acquire();          // ← ~2ns, deterministic
    // use order
    orderPool.release(order);                    // ← ~2ns, deterministic
}
```

**Use `std::array` not `std::vector` for fixed-size hot-path collections.**
`std::vector` can reallocate (heap) when it grows. `std::array` is fixed-size, stack or pre-allocated, never reallocates.

---

### Foundation 4: Dedicated Threads with Clear Ownership

**Rule:** Each thread has exactly one job. No thread does two jobs. Trading logic never runs on the same thread as UI, persistence, or networking.

**Thread map (fixed from day one):**

```
Thread 0 — HOT PATH (Core 0, pinnable)
  Owner:   market events → indicators → strategy → risk → order out
  Reads:   MarketDataRing, FillInRing
  Writes:  OrderOutRing, PersistenceRing, RoutingRing
  Never:   touches DB, Redis, UI, std::cout, file I/O
  Timing:  microseconds per event

Thread 1 — ROUTING ALGO (Core 1, pinnable)
  Owner:   reads RoutingRing, manages containers, allocates capital
  Reads:   RoutingRing (signals, container status)
  Writes:  container create/kill commands to hot path
  Timing:  milliseconds acceptable (still no DB on this thread)

Thread 2 — PERSISTENCE (Core 2)
  Owner:   reads PersistenceRing, writes to PostgreSQL and Redis
  Reads:   PersistenceRing
  Writes:  PostgreSQL, Redis
  Timing:  milliseconds, nobody is waiting

Thread 3 — EXECUTION / BROKER (Core 3)
  Owner:   sends orders to broker, receives fills
  Reads:   OrderOutRing
  Writes:  FillInRing, PersistenceRing
  Manages: broker WebSocket connection, reconnection logic

Thread 4 — API / UI (Core 4)
  Owner:   HTTP and WebSocket server for React frontend
  Reads:   read-only portfolio snapshots (published periodically by Thread 0)
  Writes:  nothing into the trading system directly
           (user commands go through a command ring buffer)
  Timing:  milliseconds, fine
```

**How threads communicate:**

```
ALL inter-thread communication = ring buffer only
NO shared mutable state
NO std::mutex between threads
NO condition_variable between threads

Read-only snapshots (portfolio state, P&L) are published by Thread 0
to a separate snapshot buffer, which Thread 4 reads.
Thread 4 never reads live trading state directly.
```

**CPU pinning (add at Level 2 phase, not day one):**
```cpp
// One line per thread, added during low-latency optimization phase
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(0, &cpuset);  // pin to core 0
pthread_setaffinity_np(hotPathThread.native_handle(), sizeof(cpuset), &cpuset);
```

The threads must be separated from day one. CPU pinning is added later in one line per thread. If threads are not separated from day one, CPU pinning cannot help.

---

## Part 2: Development Phases

Each phase has a clear entry condition, deliverable, and exit condition (definition of done). No phase begins until the previous one is complete and tested.

---

### Phase 0 — Project Skeleton and Infrastructure
**Goal:** Empty but correctly structured C++ project with all 4 foundations in place before any trading logic exists. Abstract interfaces for data providers and future extensibility points defined here.

**What to build:**
```
- CMake project structure (all modules incl. auth/, workbook/, market_data/providers/,
  no circular dependencies)
- Ring buffer implementation (lock-free, templated, unit tested)
- Memory pool implementation (templated, unit tested)
- Thread wrapper with ownership enforcement
- Logger (async, writes to persistence thread — never blocks hot path)
- Basic domain types: Symbol, Price, Quantity, Timestamp (no logic yet)
- Abstract interfaces (no concrete implementations yet):
    DataProvider          ← abstract: name(), historicalLoader(), liveFeed(), capabilities()
    MarketDataFeed         ← abstract: connect(), subscribe(), onEvent()
    HistoricalDataLoader  ← abstract: loadBars(symbol, from, to, resolution)
    DataSourceRegistry     ← holds registered providers, returns active one from config
- CI: build + run tests on every commit
```

**Libraries to add (via CMake FetchContent or vcpkg):**
```
spdlog         → async logging
Google Test    → unit testing
nlohmann/json  → config file parsing
libpqxx        → PostgreSQL (persistence thread only)
Boost.Asio     → async networking for broker/feed connections
bcrypt         → password hashing (used in auth, API thread only)
jwt-cpp        → JWT token generation/validation (API thread only)
```

**Definition of done:**
- Ring buffer: unit tests pass, verified zero allocations in hot path using valgrind/AddressSanitizer
- All 4 threads start, communicate via ring buffers, shut down cleanly
- DataProvider/DataSourceRegistry interfaces compile and can register a dummy provider
- Zero compiler warnings at `-Wall -Wextra -Wpedantic`
- Build time under 30 seconds

---

### Phase 1 — Domain Types and Event Model
**Goal:** The complete vocabulary of the system. Every other phase uses these types. Includes all new types for workbooks, auth, bar resolution, and data providers.

**What to build:**
```
domain/
  Symbol          (integer ID internally, string name in symbol table)
  Instrument      (type, lot size, tick size, currency)
  Price           (typed, not raw double)
  Quantity        (typed, lot-size aware)
  UserID          (typed UUID wrapper)
  WorkbookID      (typed UUID wrapper)
  BarResolution   (ONE_MIN | FIVE_MIN | FIFTEEN_MIN | THIRTY_MIN | ONE_HOUR | ONE_DAY | ONE_WEEK)
                  Enum defined now, only ONE_MIN used initially.
  Order           (all fields, pool-allocated)
                  → includes trading_mode: MIS | CNC
                    container reads strategy.metadata().trading_mode
                    and stamps it on every Order it creates
                  → includes workbook_id, container_id
  Fill            (all fields, pool-allocated)
                  → includes workbook_id
  Position        (per symbol)
  Account         (capital, mode, status)
  ContainerMode   (BACKTEST | PAPER | REAL)
  ContainerStatus (WARMING_UP | ACTIVE | EXITING | STOPPED)
  TradingMode     (MIS | CNC)
  User            (id, username, role: USER | ADMIN)
  Workbook        (id, user_id, name, main_capital, status: ACTIVE | PAUSED | ARCHIVED)

events/
  BarEvent        (symbol_id, timestamp, resolution, open, high, low, close, volume)
                  resolution field exists from day one — always ONE_MIN for now
  OrderIntent     (strategy_id, symbol_id, side, qty, type, price)
  FillEvent       (order_id, symbol_id, side, filled_qty, fill_price, fees)
  OrderUpdate     (order_id, status, reason)
  SystemEvent     (type: SESSION_START | SESSION_END | KILL_SWITCH |
                    MIS_SQUAREOFF_WARNING | ...)
  WorkbookEvent   (type: CREATED | CAPITAL_ADDED | ACTIVITY_BORROW | ACTIVITY_RETURN)
```

**Key rule:** All hot-path types use integer IDs for symbols, not strings. String lookup happens only at startup (build symbol table) and at output (logging, UI). Never in the hot path. WorkbookID/UserID are only used on the API thread and persistence thread — never in the hot path.

**Definition of done:**
- All types compile cleanly
- All types fit in cache-friendly sizes (check with `sizeof`, align to cache lines where needed)
- BarResolution enum works, BarEvent carries resolution field
- WorkbookID and UserID types defined and usable
- Unit tests for all domain logic (lot-size rounding, price arithmetic, etc.)

---

### Phase 2 — Indicator Library + First 3 Strategies + Simulated Exchange + First DataProvider + Backtest (2-3 Stocks)
**Goal:** First end-to-end backtest running on 2-3 NSE stocks. Something real outputs trade results. Data loaded through the DataProvider abstraction.

**What to build:**
```
indicators/
  Indicator base class (update, value, ready)
  IndicatorLibrary (shared registry, keyed by symbol_id + type + params + resolution)
    resolution included in key from day one — always ONE_MIN for now
  EMA
  VWAP
  RSI

strategies/
  Strategy base class:
    onBar, onFill, onOrderUpdate, shouldExit
    metadata() → {
      name,
      version,                ← e.g. "1.0.0" — tagged on every backtest result in DB
      trading_mode,            ← MIS | CNC — container stamps this on every Order
      required_resolution,     ← ONE_MIN (for now, extensible later)
      required_indicators
    }
  StrategyRegistry
    EMA Crossover           (trading_mode: MIS, version: "1.0.0", resolution: ONE_MIN)
    VWAP Reversion           (trading_mode: MIS, version: "1.0.0", resolution: ONE_MIN)
    Opening Range Breakout   (trading_mode: MIS, version: "1.0.0", resolution: ONE_MIN)

execution/
  ExecutionVenue interface
  SimulatedExchange
    → receives OrderIntent
    → applies slippage model (configurable)
    → applies CostCalculator for NSE transaction costs:
        STT (0.025% sell-side intraday), brokerage, SEBI charges,
        stamp duty (0.003% buy-side), GST on brokerage
        all deducted automatically — strategy never touches cost logic
    → applies MIS leverage: if trading_mode == MIS, required_margin = value / leverage_ratio
    → emits FillEvent with net_cash_impact (after all costs)

  CostCalculator (separate class, used by SimulatedExchange only)
    → configurable rates (update one class when SEBI changes rates)

market_data/
  CSVProvider (first concrete DataProvider implementation)
    → implements DataProvider interface
    → historicalLoader(): CSVHistoricalLoader (load bars from CSV file)
    → liveFeed(): null (CSV is backtest-only, no live feed)
    → capabilities(): { resolutions: [ONE_MIN], lookback: unlimited, rate_limit: N/A }
  Register in DataSourceRegistry as "csv"
  Config: { "data_source": "csv", "csv": { "data_dir": "./data/" } }

backtest/
  BacktestRunner
    → loads historical bars via DataSourceRegistry.activeProvider().historicalLoader()
    → replays them through the ring buffer in sequence
    → runs at maximum CPU speed (no real-time wait)
    → collects: fills, positions, equity curve, P&L

reporting/
  BacktestResult
    → total return, Sharpe ratio, max drawdown, win rate, avg hold time
```

**Stocks for Phase 2:** RELIANCE, INFY, TCS (liquid, well-known NSE stocks, easy to get CSV data for)

**Market data for Phase 2 (decided 2026-09-15):**
- No Upstox provider in this phase. Engine stays on `CsvProvider`.
- Pull 1-min candles **offline** (existing Upstox client in the other repo), save CSV, then backtest.
- Range: **28 calendar days** of 1-min bars — not 1 year. Upstox 1-min history is **max ~1 month per request**; 28 days is the safe window.
- Fetched 2026-09-15: `data/1min/{RELIANCE,INFY,TCS}.csv` — 7125 bars / 19 sessions each (2026-08-18 09:15 IST through 2026-09-11 15:29 IST). Three HTTP requests, one stock each.
- One instrument per Upstox request; ~30 requests/min allowed. Three stocks = three requests.
- Do **not** fetch until the user explicitly approves.

**Definition of done:**
- CSVProvider registered in DataSourceRegistry and active
- Backtest runs for all 3 strategies on 3 stocks with **28 days** of 1-min bar data (CSV)
- Data loaded through DataProvider abstraction (not directly from CSV)
- IndicatorLibrary keys include resolution (always ONE_MIN)
- Results are printed/logged with P&L, trade count, Sharpe ratio
- No memory leaks (verified with AddressSanitizer)
- Ring buffer and memory pools used throughout — zero `new` in hot path (verified)
- Backtest is deterministic: running twice with same data produces identical results

---

### Phase 3 — Trading Container + Portfolio Ledger + Risk Engine + Capital Management + Workbook Manager
**Goal:** The container lifecycle works. Capital flows correctly through workbook → activity → container. Risk blocks bad orders. Workbook capital accounting works.

**What to build:**
```
workbook/
  WorkbookManager
    → create(user_id, name, initial_capital)        → WorkbookID
    → addCapital(workbook_id, amount)                → updates main_capital and available
    → borrowCapital(workbook_id, amount)              → deducts from workbook, returns borrow_id
    → returnCapital(workbook_id, borrow_id, final_amount) → returns capital ± P&L
    → softDelete(workbook_id)                         → sets deleted_at
    → listWorkbooks(user_id)
    → getWorkbook(workbook_id)
  WorkbookCapital
    → two-level accounting: workbook pool → activity pool → container allocation

container/
  TradingContainer
    → warmup(historical_bars)
    → start()
    → upgrade(newMode)          ← BACKTEST→PAPER, PAPER→REAL
    → exit()
    → forceExit(price)
    → stop()
    → onBar(), onFill(), onOrderUpdate()

portfolio/
  PortfolioLedger (per-workbook, per-activity)
    → tracks: borrowed_capital, available_capital, allocated_capital
    → tracks: positions per symbol per container
    → tracks: realized P&L, unrealized P&L
  CapitalManager
    → allocate(container_id, amount) → success | insufficient_funds
    → release(container_id) → returns capital to activity's pool

risk/
  RiskEngine + RiskRule interface
  MaxPositionSizeRule
  DailyLossLimitRule
  KillSwitchRule
  MarketHoursRule
  ContainerCapitalRule
  DuplicateContainerRule  ← prevents same (symbol + strategy) having >1 active container
                            within the same workbook
                            multiple different strategies on same stock: ALLOWED
                            same strategy twice on same stock in same workbook: BLOCKED
                            same strategy on same stock in different workbooks: ALLOWED
  MISSquareOffRule        ← on MIS_SQUAREOFF_WARNING event (3:15 PM):
                            rejects all new MIS OrderIntents
                            triggers force-exit for all MIS containers with open positions
```

**Definition of done:**
- WorkbookManager can create workbook, borrow capital, return capital ± P&L
- Container can be created in BACKTEST mode, run, and upgraded to PAPER mode
- Capital correctly flows: workbook → activity → container → back up on exit
- Risk engine blocks oversized orders, correctly enforces daily loss limit
- DuplicateContainerRule enforced per-workbook (not globally)
- Kill switch stops all new orders immediately
- Unit tests for all capital accounting edge cases (workbook-level and activity-level)

**Status (this repo):** Phase 3 is implemented in-memory (no Postgres — that is Phase 5). Capital is two-level: workbook pool → activity `PortfolioLedger` → container allocation. BACKTEST/PAPER allocations track `paper_capital` and do not reduce activity `available_capital` until `upgrade(REAL)`. Persistence, routing, and the HTTP API are not part of this phase.

---

### Phase 4 — First Routing Algorithm + Run Manager
**Goal:** The routing algo runs, evaluates strategies, creates and manages containers. A full run works end-to-end within a workbook context.

**What to build:**
```
routing/
  RoutingAlgo base class
    (onBar, onContainerExited, onSessionStart, onSessionEnd, stop,
     createContainer, upgradeContainer, killContainer,
     evaluateStrategy, availableCapital)
  RoutingAlgoRegistry
  DefaultRouter (first implementation)
    → evaluates each strategy on each stock using recent bars
    → creates BACKTEST container if strategy looks good
    → upgrades to PAPER after N bars of good simulated performance
    → upgrades to REAL after M bars of good paper performance
    → OR creates REAL container directly if configured to skip trial phases
    → kills underperforming containers, reallocates capital

engine/
  RunManager
    → accepts RunConfig (workbook_id, capital, stocks, routing_algo_id, mode options)
    → borrows capital from workbook via WorkbookManager.borrowCapital()
    → wires all threads and ring buffers
    → starts market data, routing algo, containers
    → handles user STOP command (force-exit all containers)
    → on completion: returns capital ± P&L to workbook via WorkbookManager.returnCapital()
    → emits RunComplete with final P&L

scheduler/
  SessionScheduler
    → knows NSE market hours (9:15 AM – 3:30 PM IST)
    → fires SESSION_START, SESSION_END events
    → fires MIS_SQUAREOFF_WARNING at 3:15 PM IST every trading day
        → MISSquareOffRule in RiskEngine handles force-exit of all MIS positions
    → blocks trading outside market hours (enforced by risk engine)
```

**Definition of done:**
- Full run within workbook: enter capital + 3 stocks + DefaultRouter → capital borrowed from workbook → containers created → strategies run → fills generated → capital ± P&L returned to workbook on stop
- Routing algo correctly skips stocks where no strategy looks good
- Routing algo correctly upgrades containers through BACKTEST → PAPER → REAL
- Force-stop correctly exits all positions at current price
- Workbook available_capital correctly updated throughout

**Status (this repo):** Phase 4 DefaultRouter is in-memory, CSV-backed, no live feed. It backtests the last **15 calendar days** of 1-min data for **10 stocks × 3 strategies**. Pairs with `realized P&L > 0` skip PAPER and are created as **REAL** containers (`SimulatedExchange` + committed capital). Run capital is **₹10 crore**, split equally among winners. `REAL` still has no broker (Phase 9). Persistence/API are Phase 5.

---

### Phase 5 — Persistence Layer + Auth + API + Signal Logging + DataFetchService
**Goal:** Results are saved permanently. Auth protects the API. Workbook-scoped endpoints work. Signal logging captures strategy decisions for graphs. DataFetchService caches market data.

**This repo:** follow **`Notes/PHASE5.md`** (step 5.1 → 5.10). Stores are **SQLite** (relational) + **RocksDB** (one session blob per ticker/resolution/date), not PostgreSQL `bars`. Paper uses live WebSocket; REST “today” is a 30-min cache only. Do not implement a step until the previous one is done.

**What to build:**
```
persistence/
  Database connection pool (libpqxx, persistence thread only)
  schema.sql with all tables:
    users, workbooks, workbook_capital_events,
    runs, backtests, paper_trades, containers,
    orders, fills, positions,
    strategy_signals, routing_decisions, risk_rejections, container_events,
    bars (with resolution, data_source, fetched_at columns),
    symbol_data_coverage,
    strategies, routing_algos
  All activity tables include workbook_id column
  Tables with deleted_at column for soft-delete support
  Migration system (version-controlled schema changes)

  Repositories:
    UserRepository            (create, find by username, validate)
    WorkbookRepository        (CRUD with soft-delete, list by user_id)
    RunRepository             (save/load run configs and results, workbook-scoped)
    ContainerRepository       (save container status, P&L, workbook-scoped)
    OrderRepository           (save every order)
    FillRepository            (save every fill)
    BarRepository             (save/load historical bars, with resolution and data_source)
    SignalRepository          (strategy_signals, routing_decisions)
    RiskRejectionRepository   (risk engine rejection events)
    ContainerEventRepository  (lifecycle events: created, upgraded, killed)

auth/
  AuthService                (register, login, validateToken)
  JWT generation/validation
  Password hashing (bcrypt)

market_data/
  DataFetchService
    → ensureDataAvailable(symbol, from, to, resolution)
    → checks BarRepository for existing data
    → fetches missing ranges from activeProvider().historicalLoader()
    → stores in DB permanently (normalized OHLCV)
    → respects provider rate limits
    → updates symbol_data_coverage table

signal_logging/
  SignalLogger
    → async writer (via PersistenceRing) for strategy_signals
    → logs only when strategy.onBar() produces OrderIntents (signals-only)
    → captures indicators_snapshot_json at signal time
  RoutingDecisionLogger
    → logs routing algo create/skip/upgrade/kill decisions
  RiskRejectionLogger
    → logs when RiskEngine rejects an OrderIntent (rule name, reason)
  ContainerEventLogger
    → logs container lifecycle events (created, upgraded, exited, killed)

api/
  HTTP server (Drogon or Crow)
  Auth middleware (validates JWT on every request except /auth/*)
  Ownership middleware (workbook.user_id == authenticated user, or ADMIN)

  Endpoints (all workbook-scoped, all require auth):
    POST /auth/register, POST /auth/login, GET /auth/me

    GET    /workbooks
    POST   /workbooks
    GET    /workbooks/{wid}
    PATCH  /workbooks/{wid}                       (add capital, rename)
    DELETE /workbooks/{wid}                        (soft-delete)

    POST /workbooks/{wid}/runs/start
    POST /workbooks/{wid}/runs/{rid}/stop
    GET  /workbooks/{wid}/runs/{rid}/status
    GET  /workbooks/{wid}/runs

    GET  /workbooks/{wid}/containers
    POST /workbooks/{wid}/containers/{cid}/kill

    POST /workbooks/{wid}/backtests/start
    GET  /workbooks/{wid}/backtests/{id}
    POST /workbooks/{wid}/paper/start
    POST /workbooks/{wid}/paper/{id}/stop

    GET /workbooks/{wid}/containers/{cid}/chart?from=&to=&include=
    GET /workbooks/{wid}/portfolio
    GET /workbooks/{wid}/fills

    DELETE /workbooks/{wid}/backtests/{id}   (soft-delete)
    DELETE /workbooks/{wid}/paper/{id}       (soft-delete)
    DELETE /workbooks/{wid}/runs/{rid}       (soft-delete)

    GET /strategies
    GET /routing-algos

    GET    /admin/workbooks                  (admin only)
    DELETE /admin/workbooks/{wid}             (hard-delete, admin only)

  WebSocket:
    /ws/workbooks/{wid}/portfolio    → live P&L and capital updates
    /ws/workbooks/{wid}/containers   → container status updates
```

**Definition of done:**
- Auth works: register, login, JWT validation, ownership enforcement
- All fills, orders, positions, signals persisted correctly to PostgreSQL with workbook_id
- Signal logging captures strategy signals (signals-only) with indicator snapshots
- DataFetchService caches bars in DB, skips already-fetched data
- Soft-delete works on backtests, paper trades, runs, workbooks
- API returns correct workbook-scoped data (run status, P&L, container list)
- WebSocket pushes live updates to a test client
- Persistence never blocks the hot path (verified by timing)

---

### Phase 6 — More Strategies + Scale to 50-300 Stocks
**Goal:** Add more strategies. Verify the system handles the full stock universe efficiently.

**What to build:**
```
Additional strategies (examples):
  MeanReversionBollinger   (Bollinger Band mean reversion)
  MomentumRSI              (RSI-based momentum)
  (more added as needed)

Indicator additions:
  ATR (for position sizing)
  BollingerBand
  RollingHigh / RollingLow

Scale testing:
  Load 300 NSE stocks into the system
  Run DefaultRouter across all 300 simultaneously
  Measure: time to process all 300 bars per minute cycle
  Target: full cycle under 10ms on a standard laptop
```

**Definition of done:**
- 5+ strategies registered and working
- 300 stocks running simultaneously in backtest
- Full 300-stock bar processing cycle under 10ms
- Memory usage stable (no leaks) over a full trading day replay

---

### Phase 7 — Additional Routing Algorithms
**Goal:** Second and third routing algo added. User can select which one to use.

**What to build:**
```
AdaptiveRouter
  → more aggressive evaluation criteria
  → faster capital reallocation
  → different trial phase logic

ConservativeRouter
  → longer observation windows
  → stricter performance thresholds before upgrading to REAL
  → smaller per-stock capital allocation limits

UI addition:
  Routing algo selector on run setup screen
  Different configuration panels per routing algo
```

**Definition of done:**
- At least 2 routing algos registered and selectable from UI
- Each routing algo has its own configuration schema
- Switching routing algos does not require any engine changes

---

### Phase 8 — Frontend UI (runs in parallel from Phase 2 onwards)
**Goal:** React/TypeScript dashboard complete and connected to the backend API. Workbook-based navigation. Auth-protected. Rich graphs per container.

**What to build (milestone-based, parallel with backend phases):**
```
Milestone A (alongside Phase 2-3):
  Auth screens:
    → Login page (username + password)
    → Register page
    → JWT stored in browser, sent on every API request

  Workbook list (dashboard):
    → list of user's workbooks (name, capital, status, activity count)
    → create new workbook button (name + initial capital)
    → open existing workbook
    → delete workbook (soft-delete)

Milestone B (alongside Phase 4-5):
  Workbook view — the main work area:
    → workbook capital overview (main capital, available, borrowed)
    → add capital button (enter amount, top up)
    → three activity launch sections, each with capital input + start button:
      1. Routing algo run: select routing algo, stocks, enter capital, Start
      2. Manual backtest: select stock, strategy, date range, enter capital, Start
      3. Manual paper trade: select stock, strategy, enter capital, Start
    → list of all activities (runs, backtests, paper trades) — current + historical
    → emergency STOP button (for active runs/paper trades)

Milestone C (alongside Phase 6-7):
  Per-container charts (the core analytical view):
    → candlestick chart (OHLCV bars from chart API)
    → indicator overlays (EMA, VWAP, RSI from signal indicator snapshots)
    → entry/exit markers (buy green, sell red from fills)
    → risk rejection markers (blocked orders, from risk_rejections)
    → container mode badges (BACKTEST→PAPER→REAL transitions)
    → all layers toggleable
  Workbook summary chart:
    → equity curve across all activities
    → one line per container (color-coded by strategy)
  Per-container detail panel:
    → trade history (fills log)
    → P&L breakdown
    → signal log

Milestone D (final):
  Delete functionality:
    → delete individual backtests, paper trades, completed runs from workbook
    → soft-delete from UI
  Admin panel:
    → login as admin → see all users' workbooks
    → open, modify, delete any workbook
    → hard-delete option
  Multi-routing-algo selector
  Strategy configuration per run
```

**Tech stack:** React, TypeScript, TailwindCSS, Recharts or lightweight-charts (for candlestick + overlay charts), WebSocket API

---

### Phase 9 — Live Market Data + Broker Integration
**Goal:** Connect to real NSE data feed and real (or paper) broker API. First real DataProvider replaces CSV.

**What to build:**
```
market_data/providers/
  [ChosenBroker]Provider   implements DataProvider
    → name(): "zerodha" or "upstox" etc.
    → historicalLoader(): fetches historical bars from broker API
    → liveFeed(): connects to broker WebSocket for live bars
    → capabilities(): supported_resolutions, rate_limit, lookback_days
  Register in DataSourceRegistry
  Config: { "data_source": "zerodha", "zerodha": { "api_key": "...", "access_token": "..." } }

  DataFetchService automatically caches bars from this provider in DB
  Subsequent backtests reuse cached data — no re-fetching

execution/
  BrokerGateway interface
  [ChosenBroker]Gateway    implements BrokerGateway
    → submit Order → broker order ID
    → cancel Order
    → receive fills and order updates
    → handle reconnection
    → map broker order states to internal OrderStatus

Paper trading:
  TradingContainer in PAPER mode connects to live DataProvider's feed
  but uses SimulatedExchange for fills
  → real market data, simulated execution
```

**Definition of done:**
- New DataProvider registered and selectable via config (switching from CSV to live)
- Live 1-min bars flowing for all 300 stocks from real provider
- DataFetchService correctly caches live provider's historical data in DB
- Paper trade runs end-to-end on live data with simulated fills
- Broker connection handles disconnect/reconnect without losing state
- Live trading run works with small capital for validation

---

### Phase 10 — Low Latency Optimizations (one by one, measure before and after each)

**Rule:** Measure first. Optimize second. Never optimize without a before/after benchmark.

**Tools for measuring:**
```
perf              → Linux performance counters, cache misses, branch mispredictions
Google Benchmark   → micro-benchmarks for hot-path functions
std::chrono        → nanosecond timestamps around critical sections
RDTSC              → CPU cycle counter (more precise than chrono for hot path)
Valgrind/Cachegrind → cache behavior analysis
```

**Optimizations in order (each measured, committed only if it improves latency):**
```
10.1 — CPU pinning + thread isolation
  Pin Thread 0 (hot path) to an isolated CPU core
  Use isolcpus kernel boot parameter to keep OS off that core
  Expected gain: eliminate OS scheduler jitter (~10-100 microseconds)

10.2 — NUMA awareness
  Allocate hot-path memory on the same NUMA node as the pinned CPU
  Expected gain: eliminate cross-NUMA memory latency (~50-100ns per access)

10.3 — Huge pages
  Allocate ring buffers and memory pools using 2MB huge pages
  Reduces TLB misses in the hot path
  Expected gain: 10-30% reduction in memory access time for large pools

10.4 — Cache-line alignment audit
  Audit all hot-path structs for false sharing
  Align frequently-read fields to 64-byte cache line boundaries
  Separate hot and cold fields in structs
  Expected gain: eliminate false sharing cache invalidations

10.5 — SIMD indicator calculations
  Vectorize EMA, RSI, ATR calculations using AVX2 or AVX-512
  Process multiple symbols' indicator updates in parallel using SIMD
  Expected gain: 4-8x throughput on indicator calculation

10.6 — Compiler optimizations
  Profile-guided optimization (PGO): compile, run workload, recompile with profile
  Link-time optimization (LTO)
  Target-specific: -march=native -O3 -funroll-loops
  Expected gain: 10-30% overall hot-path speed

10.7 — Lock-free order book (if needed)
  For strategies that need bid/ask spread: maintain an in-memory order book
  Lock-free, cache-friendly, updated on every tick

10.8 — Kernel bypass networking
  Replace standard BSD socket NSE feed with DPDK or RDMA
  Bypasses OS network stack entirely
  Expected gain: reduce network latency from ~50 microseconds to ~1 microsecond
  Requires: supported NIC, co-location at NSE data center

10.9 — Co-location at NSE
  Move servers to NSE co-location facility (NSCCL co-lo)
  Physical proximity to exchange matching engine
  Expected gain: reduce round-trip from ~5ms (internet) to ~50 microseconds (co-lo)

10.10 — FPGA order entry (optional, maximum HFT)
  Offload order generation and submission to FPGA hardware
  FPGA processes market data and submits orders in nanoseconds
  Expected gain: sub-microsecond order-to-wire
  Cost: high engineering effort, specialized hardware
```

---

### Phase 11 — Production Hardening

**Goal:** System is safe and recoverable for real-money operation.

**What to build:**
```
Recovery:
  On restart after crash: reload open positions from DB
  Reconcile with broker: compare our positions with broker's positions
  Detect and alert on discrepancies before resuming

Monitoring:
  Latency percentiles logged per bar cycle (p50, p95, p99, p999)
  Alert if p99 latency exceeds threshold
  Alert if ring buffer utilization exceeds 80%
  Alert if persistence thread is falling behind

Audit trail:
  Every order, fill, position change, container event written to immutable log
  Log is tamper-evident (append-only)

Emergency controls:
  Kill switch reachable via: UI button, API call, direct process signal
  Hard-coded daily loss limit that cannot be overridden from UI
  Position limit per symbol that cannot be exceeded by any route
  Maximum single-order size limit at execution layer
```

---

## Summary: Phase Order and Dependencies

```
Phase 0:  Project skeleton, ring buffers, memory pools, threads, DataProvider/DataSourceRegistry interfaces
Phase 1:  Domain types and events (incl. WorkbookID, BarResolution, UserID, WorkbookEvent)
Phase 2:  Indicators + 3 strategies + simulated exchange + CSVProvider + backtest (3 stocks)
Phase 3:  Trading container + workbook manager + portfolio (per-workbook) + risk engine
Phase 4:  Routing algo + run manager (workbook-scoped, 3 stocks, 1 routing algo, 3 strategies)
Phase 5:  Persistence + auth + API (workbook-scoped) + signal logging + DataFetchService + delete
Phase 6:  More strategies + scale to 300 stocks
Phase 7:  Additional routing algos
Phase 8:  Frontend UI  [parallel from Phase 2, milestone-based: auth, workbook list, workbook view,
          per-container charts with signals/fills overlay, admin panel, delete UI]
Phase 9:  Live data feed + broker integration (new DataProvider, replaces CSV)
Phase 10: Low latency optimizations (one by one, measured)
Phase 11: Production hardening

Target at end of Phase 4: Working system with workbooks, 3 stocks, 1 routing algo, 3 strategies
Target at end of Phase 5: Fully persisted, auth-protected, workbook-scoped API, chart data ready
Target at end of Phase 7: Full system, 300 stocks, multiple routing algos, full UI with graphs
Target at end of Phase 9: Live trading capable, real data provider, DB-cached market data
Target at end of Phase 10: HFT-grade (Level 2)
```

---

## Future Improvements (deferred — add after core system is stable)

These items are confirmed as valuable but deliberately deferred.
Each is designed to slot into existing modules without structural changes.

---

### F1 — Walk-Forward Testing
**Where:** `backtest/BacktestRunner` extension
**What:** Automated systematic train/test window rolling. Not just changing date range manually — the runner automatically divides history into rolling windows, trains (finds best params) on each, tests on the next unseen window, and reports average test-period performance. Prevents overfitting.

---

### F2 — Market Regime Detection
**Where:** `indicators/` — as a composite indicator
**What:** Classifies current market as TRENDING_UP | TRENDING_DOWN | SIDEWAYS | HIGH_VOLATILITY using ADX, ATR ratio, and rolling return correlation. Routing algo reads regime before choosing which strategies to activate on a stock.

---

### F3 — Data Quality and Corporate Actions
**Where:** `market_data/` — data loading pipeline
**What:** Adjusted price series for stock splits, bonus issues, dividends. Prevents backtests from showing fake -50% drops on split dates. Use adjusted close prices from data provider.

---

### F4 — Circuit Breaker Rule
**Where:** `risk/CircuitBreakerRule` + `domain/Instrument` (add circuit_pct field)
**What:**
- `Instrument` stores `upper_circuit_price` and `lower_circuit_price` (refreshed from broker/NSE API at 9:00 AM each day)
- `CircuitBreakerRule` rejects OrderIntents where current price >= upper_circuit or <= lower_circuit
- Market data feed emits `MarketStatusEvent{CIRCUIT_LOCKED}` when feed stops for a symbol
- RiskEngine maintains set of circuit-locked symbols, updated reactively from feed events

**Note:** Currently treated as non-existent. Add when live broker integration begins.

---

### F5 — Fill Model Realism (Market Impact)
**Where:** `execution/SimulatedExchange` — improve CostCalculator and fill model
**What:** Dynamic slippage based on order size relative to bar volume. Small orders fill near close. Large orders (>1% of avg daily volume) get worse fills due to market impact. Makes backtests more predictive of live performance.

---

### F6 — Strategy Parameter Optimization
**Where:** New module `optimization/ParameterOptimizer`
**What:** Wraps `BacktestRunner`. Takes a strategy + parameter ranges (e.g. fast_period in [5,8,10,12,15]) and runs a backtest for every combination. Ranks by Sharpe ratio on walk-forward test periods. Outputs best validated parameter set. Zero changes to strategy code or engine.

---

### F7 — Alerting System
**Where:** New module `alerts/AlertManager` — runs on API thread (Thread 4)
**What:** Monitors system events and sends notifications on:
- Kill switch activated
- Daily loss limit hit
- Broker connection dropped > N seconds
- Market data feed disconnected
- Available capital below minimum threshold
- P&L deviation beyond expected range

**Channels:** Email first, SMS/push notification later.

---

### F8 — Multi-Resolution Bar Support
**Where:** `market_data/BarSynthesizer` + `IndicatorLibrary` + `Strategy`
**What:** Enable strategies to use bar resolutions beyond ONE_MIN (FIVE_MIN, FIFTEEN_MIN, ONE_HOUR, ONE_DAY, etc.).
- `BarSynthesizer` aggregates N one-min bars into one higher-resolution bar (e.g. 15 one-min → 1 fifteen-min)
- Synthesized bars stored in DB (`bars` table already has `resolution` column)
- `IndicatorLibrary` key already includes `resolution` — separate indicator instances per resolution
- `Strategy.metadata().required_resolution` already exists — just set to a higher value
- `TradingContainer` subscribes to the correct resolution bar stream

**Why deferred:** Initial implementation is ONE_MIN only. All interfaces are designed from day one to support this — zero structural changes required when implemented.

---

### F9 — Additional Data Providers
**Where:** `market_data/providers/` — new DataProvider implementations
**What:** Add more third-party data sources (Upstox, AngelOne, Dhan, etc.).
- Each new provider implements `DataProvider` interface (historicalLoader, liveFeed, capabilities)
- Registered in `DataSourceRegistry`
- Selected via config — one active at a time
- `DataFetchService` automatically caches data from any provider in the same DB format

**Why deferred:** CSVProvider is sufficient for backtesting phases. Real providers added alongside broker integration (Phase 9).

---

### F10 — Advanced Auth (OAuth, SSO)
**Where:** `auth/AuthService` extension
**What:** Replace or augment basic username/password with OAuth2 providers (Google, GitHub) or enterprise SSO. JWT token format stays the same — only the authentication method changes. API middleware and workbook ownership enforcement are unchanged.

**Why deferred:** Basic auth is sufficient for initial development. Extensible design means zero structural changes when upgraded.

---

```
1. No std::mutex in the hot path. Ever.
2. No heap allocation (new/delete) in the hot path. Ever.
3. No DB call, Redis call, or file I/O on Thread 0 or Thread 1. Ever.
4. Every optimization must be benchmarked before and after. No blind optimizations.
5. Every phase must pass tests before the next phase begins.
6. Backtest must remain deterministic: same input always produces same output.
7. Strategy code must be identical across BACKTEST, PAPER, and REAL modes.
8. The kill switch must work even if the API server is down.
9. No secrets (broker API keys, credentials) in source code or config files in repo.
10. Compiler warnings are treated as errors: -Wall -Wextra -Werror from day one.
11. All workbook data (runs, fills, signals) must be permanently persisted — no data loss on restart.
12. Auth is API-layer only — never in the hot path or trading engine.
13. Soft-delete by default for user-facing data — hard-delete only by admin.
```
