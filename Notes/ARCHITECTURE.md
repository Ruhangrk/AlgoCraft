# C++ Algorithmic Trading Platform — Architecture Document

---

## 1. Vision

A fully C++ trading platform for NSE equities (expandable to other markets and instruments) that:

- Runs a **Routing Algorithm** that continuously evaluates stocks and strategies, allocates capital, and manages Trading Containers
- Supports **multiple strategies** and **multiple routing algorithms** — pluggable, additive, no engine changes required
- Uses **one shared codebase** across backtest, paper trading, and live trading — same strategy logic, same container, same risk engine
- Allows the UI to manually **backtest or paper trade** any stock with any strategy independently of the routing algo
- Scales from 2-3 strategies and 1 routing algo today to N strategies and M routing algos tomorrow
- Organizes all user work into **Workbooks** — isolated workspaces with their own capital pool, history, and graphs
- Supports **multiple users** with basic auth — each user sees only their own workbooks, admin sees all
- Caches **market data in DB** to minimize third-party API calls — fetch once, reuse forever
- Supports **pluggable data sources** — swap providers via config, no code changes
- Provides **rich UI graphs** per container — candlestick charts with indicator overlays, entry/exit markers, and signal annotations, all permanently stored

---

## 2. Architecture Principles

| Principle | Meaning |
|---|---|
| Open / Closed | Add new strategies, routing algos, markets, data sources by writing new code — never by modifying the core engine |
| Single source of truth | One strategy implementation runs in backtest, paper, and live — no duplication |
| Reuse via abstraction | Container, Strategy, Indicator, RiskEngine, ExecutionVenue, DataProvider are base classes — all modes share them |
| Event-driven | Components communicate via events, not direct calls — decoupled and thread-safe |
| Capital sovereignty | Workbook owns the capital pool — activities (runs, backtests, paper trades) borrow capital from it, return capital ± P&L on completion |
| Workbook isolation | Each workbook is a fully independent workspace — its own capital, its own history, its own graphs. No cross-workbook state sharing |
| Explicit kill switch | Emergency stop is enforced inside the C++ engine itself, not just at the API layer |
| Fetch once, reuse forever | Historical market data fetched from third-party APIs is stored permanently in DB. Same data is never re-fetched |

---

## 3. System Overview

```
React / TypeScript Frontend
            |
            | HTTP / WebSocket
            v
Auth Layer (JWT — validates every request)
            |
            v
C++ API Layer
            |
            v
Workbook Manager (capital pool, activity lifecycle)
            |
            v
Run Manager
   ├── Routing Algo Runner
   |        |
   |        | creates/kills
   |        v
   |   Trading Containers (1 stock + 1 strategy + capital)
   |        |
   |        | submits orders
   |        v
   |   Execution Venue (Simulated or Real Broker)
   |
   ├── Manual Backtest Runner (stock + strategy, no routing algo)
   └── Manual Paper Trade Runner (stock + strategy, no routing)
      |
      ├── Indicator Library (shared across all)
      ├── Portfolio Ledger (per workbook — capital and positions)
      ├── Risk Engine (all orders go through this)
      └── Market Data Layer
              |
              | BarEvent / TickEvent
              v
         DataFetchService (DB cache → API fallback)
              |
              v
         DataProvider (active: one of Zerodha/Upstox/CSV/...)
              |
              v
         DataSourceRegistry
              |
              v
         PostgreSQL + Redis
```

---

## 4. Layer-by-Layer Design

---

### 4.1 Domain Types — the universal language

Zero dependencies. Every other layer speaks these types.

```
UserID          → typed UUID wrapper
WorkbookID      → typed UUID wrapper — every activity in the system belongs to a workbook

Symbol
  - market:     NSE | BSE | CRYPTO | ...
  - segment:    EQ | FO | ...
  - ticker:     "RELIANCE", "INFY", ...

Instrument
  - symbol
  - type:       EQUITY | FUTURE | OPTION | CRYPTO | COMMODITY
  - lot_size, tick_size, currency
  - expiry      (for derivatives)

Price           → typed wrapper, never raw double
Quantity        → typed, lot-size aware
Capital         → typed, currency-aware

BarResolution   → ONE_MIN | FIVE_MIN | FIFTEEN_MIN | THIRTY_MIN | ONE_HOUR | ONE_DAY | ONE_WEEK
                  Enum exists from day one for future extensibility.
                  Initial implementation uses ONE_MIN only.

Order
  - order_id, symbol, side (BUY/SELL)
  - type: MARKET | LIMIT | SL | SL-M
  - trading_mode: MIS | CNC        ← declared by strategy metadata, carried by every order
                                      MIS = intraday leverage, must exit by 3:15 PM
                                      CNC = delivery, can hold overnight
  - quantity, limit_price
  - workbook_id, container_id, strategy_id, routing_algo_id

Fill
  - order_id, symbol, side
  - filled_qty, fill_price, timestamp, fees
  - workbook_id

Position
  - symbol, net_qty, average_price
  - unrealized_pnl, realized_pnl

ContainerMode   → BACKTEST | PAPER | REAL
ContainerStatus → WARMING_UP | ACTIVE | EXITING | STOPPED
```

---

### 4.2 Event Model — the nervous system

Everything that happens in the system is an event. No component calls another directly.

```
MarketEvents:
  BarEvent          → OHLCV for a symbol (carries resolution field — ONE_MIN for now)
  TickEvent         → individual trade/quote
  MarketStatusEvent → SESSION_OPEN | SESSION_CLOSE | HALT | MIS_SQUAREOFF_WARNING
                       MIS_SQUAREOFF_WARNING fires at 3:15 PM IST
                       → RiskEngine blocks new MIS OrderIntents
                       → MISSquareOffRule force-exits all open MIS positions

OrderEvents:
  OrderIntent       → strategy wants to place an order
  OrderSubmitted    → sent to execution venue
  OrderFilled       → partial or full fill received
  OrderCancelled
  OrderRejected

ContainerEvents:
  ContainerCreated
  ContainerUpgraded   → PAPER promoted to REAL
  ContainerExited     → strategy exited or routing algo killed it
  ContainerStopped    → force-stopped by user

WorkbookEvents:
  WorkbookCreated
  WorkbookCapitalAdded     → user added more capital to workbook
  ActivityCapitalBorrowed  → capital deducted from workbook for a run/backtest/paper trade
  ActivityCapitalReturned  → capital ± P&L returned to workbook on activity completion

SystemEvents:
  WarmupComplete      → historical data loaded, indicators ready
  KillSwitchActivated
  RunStopped          → user stopped the routing algo run
```

---

### 4.3 Market Data Layer

**Core interfaces (unchanged by any data source):**

```
MarketDataFeed (abstract interface)
  connect()
  subscribe(symbol)
  unsubscribe(symbol)
  onEvent(MarketEvent callback)

HistoricalDataLoader (abstract interface)
  loadBars(symbol, from, to, resolution) → vector<BarEvent>
```

**DataProvider — groups a historical loader + live feed per vendor:**

```
DataProvider (abstract interface)
  name()                → "zerodha" | "upstox" | "angelone" | "csv" | ...
  historicalLoader()    → HistoricalDataLoader&
  liveFeed()            → MarketDataFeed&      ← may be null for backtest-only providers (e.g. CSV)
  capabilities()         → DataProviderCapabilities {
        supported_resolutions,        ← e.g. [ONE_MIN, FIVE_MIN, ONE_DAY]
        max_historical_lookback_days, ← how far back API can go
        rate_limit_per_second,        ← API rate limit
        markets_supported             ← [NSE_EQ, NSE_FO, ...]
      }
```

**DataSourceRegistry — holds all providers, returns the active one:**

```
DataSourceRegistry
  register<ZerodhaProvider>("zerodha")
  register<UpstoxProvider>("upstox")
  register<CSVProvider>("csv")
  activeProvider()      → DataProvider&    ← selected by config file, one at a time
```

Switching data source = change one config value + restart. Zero code changes.
Initial implementation: one concrete provider only (CSV for backtesting).

**DataFetchService — fill-on-demand with DB caching:**

```
DataFetchService
  ensureDataAvailable(symbol, from, to, resolution)
    → checks bars table in DB for existing data
    → only calls activeProvider().historicalLoader() for missing date ranges
    → stores fetched bars in DB permanently (normalized OHLCV, provider-agnostic)
    → enforces rate limits from provider.capabilities()

  Once a trading day is closed and stored, it is never re-fetched.
  Current day: updated from live feed, written to DB at session end.
```

**BarSynthesizer — for future multi-resolution support:**

```
BarSynthesizer (deferred — not needed for ONE_MIN only)
  synthesize(vector<BarEvent> source_bars, target_resolution) → vector<BarEvent>
    → aggregates N source bars into one higher-resolution bar
    → e.g. 15 one-min bars → 1 fifteen-min bar
    → synthesized bars stored in DB for reuse
```

**Normalization responsibility:** Each provider implementation is fully responsible for converting its vendor-specific wire format (JSON, Protobuf, FIX, etc.) into `BarEvent` before writing to the `MarketDataRing`. Everything beyond the ring buffer is vendor-blind — Thread 0, indicators, strategies, and containers only ever see `BarEvent`.

**Adding a new data source:**
1. Create class `DhanProvider : public DataProvider`
2. Implement `historicalLoader()` and `liveFeed()` with vendor-specific API calls
3. Register: `DataSourceRegistry::register<DhanProvider>("dhan")`
4. Done — select it in config, no engine changes

To add a new market later: write a new provider. Engine does not change.

---

### 4.4 Indicator Library — shared, stateful, reusable

Indicators are stateful objects maintained by the engine. They update incrementally on each new bar. No recalculation from scratch per tick.

```
Indicator (abstract base)
  update(BarEvent)  → void
  value()           → double
  ready()           → bool    → false until enough bars seen (warmup period)
```

Built-in implementations:
```
  EMA(period), SMA(period), VWAP, RSI(period),
  ATR(period), RollingHigh(period), RollingLow(period), BollingerBand(period, stddev)
```

**Adding a new indicator type** (e.g. MACD):
inherit `Indicator`, implement three methods, add to CMakeLists, recompile — immediately usable in any strategy.

**Internals — two maps, one purpose each:**
- `indicator_map` keyed by `(symbol_id, type, params, resolution)` — owns instances, handles deduplication
- `symbol_to_indicators` keyed by `symbol_id` — flat list of pointers per stock, used by Thread 0 for fast per-bar updates

When a RELIANCE bar arrives, only RELIANCE's indicators are updated. INFY and TCS indicators are not touched. Each stock is fully independent.

**Auto-registration:** A strategy calls `lib.get<EMA>(symbol, period=9)` once inside `configure()` at container creation. The library creates the instance if it doesn't exist, adds it to that symbol's update group, and returns a reference the strategy holds as a member pointer.
If two containers on the same stock need the same indicator — one instance, both read it. No redundant computation.

**Ordering guarantee:** Within every bar cycle on Thread 0, indicators for a symbol are always updated *before* any container for that symbol receives the bar. Strategy `onBar()` always reads fresh values — never stale.

**Resolution is part of the key from day one.** Currently always `ONE_MIN`. Adding higher resolutions later needs zero structural changes — just new entries with a different resolution key.

---

### 4.5 Strategy Layer — pluggable

```
Strategy (abstract base)
  configure(StrategyConfig, IndicatorLibrary&)
  onBar(BarEvent, PortfolioView)   → vector<OrderIntent>
  onFill(FillEvent)                → void
  onOrderUpdate(OrderEvent)        → void
  shouldExit()                     → bool
  metadata()                       → {
        name,
        version,               ← e.g. "1.0.0", stored with backtest results
        trading_mode,           ← MIS | CNC, declared once per strategy
        required_resolution,   ← ONE_MIN (for now, extensible later)
        required_indicators
      }
```

**Strategy version rule:** Every strategy declares a version string. Every backtest result stored in DB is tagged with `strategy_name + strategy_version + params`. This lets you compare v1 vs v2 results objectively and reproduce any historical backtest exactly.

**Trading mode rule:** Strategy declares `trading_mode` once in metadata. Container reads it and attaches it to every `Order` it creates. Strategy never writes order submission logic — it only generates `OrderIntent`.

**Adding a new strategy:**
1. Inherit `Strategy`
2. Implement the above methods
3. Register in `StrategyRegistry`
4. Done — zero engine changes

**First 3 strategies (NSE equities, 1-min bars):**

| Strategy | Logic |
|---|---|
| EMA Crossover | Fast EMA crosses above/below slow EMA → buy/sell signal |
| VWAP Reversion | Price deviates significantly from intraday VWAP → reversion trade |
| Opening Range Breakout | First N minutes define a high/low range; breakout above/below triggers entry |

These cover trend-following, mean-reversion, and momentum — three distinct signal types good for validating the full framework.

---

### 4.6 Trading Container — the core execution unit

**One container = one stock + one strategy + allocated capital.**

The container is the most important reuse point in the system. It is identical for paper and real trading — only the execution venue changes.

```
TradingContainer
  container_id
  symbol
  strategy              → Strategy instance
  allocated_capital      → capital borrowed from routing algo's pool
  mode                  → BACKTEST | PAPER | REAL
  status                → WARMING_UP | ACTIVE | EXITING | STOPPED

  // lifecycle
  warmup(historical_bars)  → feeds bars to strategy indicators until ready
  start()                  → begin receiving events (historical replay or live)
  upgrade()                → BACKTEST → PAPER → REAL progression
                              switches data source and/or execution venue
                              preserves strategy state: indicators, rolling windows
  exit()                   → strategy-initiated graceful exit
  forceExit(price)         → routing algo or user kills, exit at current rate
  stop()                   → cleanup, return capital to routing algo pool

  // per mode:
  //   BACKTEST → HistoricalLoader  + SimulatedExchange  (no capital committed)
  //   PAPER    → LiveMarketFeed    + SimulatedExchange  (no capital committed)
  //   REAL     → LiveMarketFeed    + BrokerGateway       (capital allocated)

  // event handlers
  onBar(BarEvent)
    → strategy.onBar() → OrderIntents
    → each intent → RiskEngine.check()
    → approved → ExecutionVenue.submit()

  onFill(FillEvent)
    → strategy.onFill()
    → Portfolio.update()
    → if strategy.shouldExit() → exit()

  onOrderUpdate(OrderEvent)
    → strategy.onOrderUpdate()
```

**Upgrade path — BACKTEST → PAPER → REAL (code reuse):**

```
Routing algo creates container in BACKTEST mode
  → runs on recent historical data, SimulatedExchange, no capital committed
  → routing algo evaluates: signals, simulated P&L, drawdown

  → satisfied → container.upgrade() to PAPER
      → data source: HistoricalLoader → LiveMarketFeed
      → execution venue stays: SimulatedExchange
      → no capital committed yet, strategy state preserved

  → satisfied → container.upgrade() to REAL
      → execution venue: SimulatedExchange → BrokerGateway
      → capital allocated from routing algo pool
      → mode = REAL, strategy state preserved (indicators unchanged)
```

---

### 4.7 Routing Algorithm — the master orchestrator

**Routing algo is the highest-level decision maker within a run.** It borrows capital from the workbook's pool, evaluates strategies against stocks, creates and manages containers, and runs continuously until the user stops it.

```
RoutingAlgo (abstract base)
  configure(RoutingConfig)                    → capital, stock universe, params
  onBar(BarEvent, MarketSnapshot)             → called every bar for every symbol
  onContainerExited(ContainerEvent)           → a container finished, capital returned
  onSessionStart()
  onSessionEnd()
  stop()                                       → user-initiated stop

  // tools available to routing algo implementation
  createContainer(symbol, strategy_id, capital, mode: BACKTEST|PAPER|REAL) → ContainerID
    // routing algo freely chooses starting mode:
    //   BACKTEST → test on history first, upgrade later
    //   PAPER    → observe on live data first, upgrade later
    //   REAL     → go directly to real trading, no trial phase
  upgradeContainer(container_id)
  killContainer(container_id)
  getContainerStatus(container_id)        → ContainerStatus + P&L + positions
  evaluateStrategy(symbol, strategy_id, lookback) → StrategyEvalResult
    ← runs strategy in observation mode on recent data, returns metrics
  availableCapital()                      → how much capital is unallocated
  allocatedCapital()                      → how much is in active containers
```

**Routing algo flow:**

```
User opens workbook, enters capital=₹10,00,000 for this run, selects stocks=[RELIANCE, INFY, TCS, ...300 stocks], clicks START
  → ₹10,00,000 deducted from workbook's main capital

Routing algo starts:
  every bar (every minute):
    for each stock in universe:
      1. evaluate each strategy on this stock
         → run in observation mode on recent N bars
         → score: signal strength, volatility, trend clarity, etc.
      2. decide:
         - no strategy looks good → skip this stock, keep watching

         - strategy looks good → routing algo chooses ONE of:
             A. create BACKTEST container → observe history → upgrade to PAPER → upgrade to REAL
             B. create PAPER container    → observe live    → upgrade to REAL
             C. create REAL container     → go directly to real trading (no trial phase)
           (which path to take is entirely the routing algo's logic — it can even
           be configured by the user when they set up the run)

    - already have BACKTEST/PAPER container → check simulated performance
        → good → upgrade() to next mode
        → bad  → kill container, try different strategy or wait

    - already have REAL container → monitor live P&L and risk
        → underperforming → kill it, capital returns to pool
        → performing well → leave it running
3. if multiple strategies look good for one stock:
    → create multiple containers (one per strategy)
    → split capital: e.g. ₹2L to EMACrossover, ₹3L to VWAPReversion
4. manage total capital:
    → never exceed available capital across all containers
    → respect per-stock and per-strategy allocation limits

on user STOP:
  → kill switch activated
  → for each active container: forceExit(current_price)
  → wait for all exits (with timeout)
  → capital reconciled
  → run capital ± P&L returned to workbook's main capital
  → run marked STOPPED
```

**Adding a new routing algo:**
1. Inherit `RoutingAlgo`
2. Implement `onBar()` and container management logic
3. Register in `RoutingAlgoRegistry`
4. Done — user can select it from UI

---

### 4.8 Portfolio Ledger and Capital Management

**Each workbook has its own PortfolioLedger. Different workbooks are fully isolated.**

```
WorkbookCapital (per workbook)
  main_capital           → total capital the user has put into this workbook
  available_capital      → main_capital minus all currently borrowed amounts
  borrowed_capital        → sum of capital currently lent to active runs/backtests/paper trades

PortfolioLedger (per active run/backtest/paper trade within a workbook)
  borrowed_capital        → amount this activity borrowed from the workbook
  available_capital       → not yet allocated to any container within this activity
  allocated_capital       → sum of all container allocations within this activity
  paper_capital           → capital in PAPER containers (not yet real)
  realized_pnl            → across all closed containers in this activity
  unrealized_pnl          → across all active containers in this activity
  positions               → per symbol, across all containers in this activity
  fills_history           → complete audit trail for this activity
```

Capital accounting rules (two-level):

**Workbook level:**
- User creates workbook and enters ₹X → `main_capital = X`, `available_capital = X`
- User adds more capital later → `main_capital += Y`, `available_capital += Y`
- User starts a routing algo run with ₹A → `workbook.available_capital -= A`
- User starts a manual backtest with ₹B → `workbook.available_capital -= B`
- User starts a manual paper trade with ₹C → `workbook.available_capital -= C`
- Activity finishes → `workbook.available_capital += (borrowed ± P&L)`

**Activity level (within a run):**
- Routing algo creates container with ₹Y → `activity.available_capital -= Y`
- Container exits with value ₹Z → `activity.available_capital += Z`, `realized_pnl += (Z - Y)`
- Paper containers do not reduce `activity.available_capital` until upgraded to REAL

---

### 4.9 Risk Engine — universal gatekeeper

Every `OrderIntent` from every strategy in every container passes through the risk engine before it becomes an order. No exceptions.

```
RiskEngine
  check(OrderIntent, ContainerContext, PortfolioLedger) → APPROVED | REJECTED(reason)
```

Rules (additive — add new rules without changing engine):
```
  MaxPositionSizeRule       → per symbol
  MaxExposureRule           → total portfolio exposure limit
  DailyLossLimitRule        → stop trading if daily loss exceeds threshold
  ContainerCapitalRule      → order qty cannot exceed container's allocated capital
  KillSwitchRule            → if kill switch active, reject everything
  DuplicateOrderRule        → prevent double submission
  MarketHoursRule           → reject orders outside market session
  DuplicateContainerRule    → prevents same (symbol + strategy) pair from having
                               more than one active container within the same workbook
                               multiple different strategies on same stock: ALLOWED
                               same strategy twice on same stock in same workbook: BLOCKED
                               same strategy on same stock in different workbooks: ALLOWED
  MISSquareOffRule          → at 3:15 PM, rejects all new MIS OrderIntents
                               triggers force-exit for all MIS containers with open positions
                               enforces NSE intraday square-off deadline
```

Adding a new risk rule: write one class, register it. All containers and all strategies respect it automatically.

**Future rules (deferred):**
```
CircuitBreakerRule   → checks if symbol has hit NSE upper/lower circuit limit
                        requires: Instrument stores circuit_pct, daily circuit prices
                        refreshed from broker/NSE API each morning
                        deferred: treat circuit limits as non-existent for now
```

---

### 4.10 Execution Layer — pluggable venue

```
ExecutionVenue (abstract interface)
  submitOrder(Order)     → order_id
  cancelOrder(order_id)
  onOrderEvent(callback)

SimulatedExchange   implements ExecutionVenue
  → used for PAPER containers and backtest
  → applies fill model: current price + slippage
  → applies CostCalculator for all NSE transaction costs
  → applies MIS leverage when order.trading_mode == MIS
  → configurable: instant fill vs realistic partial fills

CostCalculator (inside SimulatedExchange):
  → STT:              0.025% of sell-side value (intraday equity)
  → Brokerage:        configurable (flat ₹20 or percentage)
  → SEBI charges:     0.0001% of turnover
  → Stamp duty:       0.003% of buy-side value
  → GST:              18% of brokerage
  → All deducted automatically from every simulated fill
  → Strategy never knows about costs — they are infrastructure

MIS leverage (inside SimulatedExchange):
  → when order.trading_mode == MIS:
      required_margin = order_value / leverage_ratio
      (leverage_ratio configurable, default 5x for NSE equities)
      portfolio checks available_margin, not full capital
  → when order.trading_mode == CNC:
      required_capital = full order_value (no leverage)

BrokerGateway (abstract)
  → used for REAL containers
  → one concrete implementation per broker

ZerodhaGateway      implements BrokerGateway   (add when broker decided)
AngelOneGateway     implements BrokerGateway   (add when broker decided)
```

Strategy and container code **never know** which venue is active. Container holds a pointer to `ExecutionVenue`. In PAPER mode it points to `SimulatedExchange`. After `upgrade()`, it points to the real `BrokerGateway`.

---

### 4.11 Manual Backtest and Paper Trade (UI feature)

Independent of routing algo. User selects one stock + one strategy from UI **within a workbook**.
Both activities borrow capital from the workbook's main capital pool.

**Backtest:**
```
User opens workbook, picks: stock=RELIANCE, strategy=EMA Crossover,
  capital=₹1,00,000, from=2023-01-01, to=2024-01-01, clicks START
  → ₹1,00,000 deducted from workbook.available_capital
  → DataFetchService ensures bars exist in DB (fetches from API if missing)
  → BacktestRunner creates a TradingContainer (mode=BACKTEST)
  → replays bars in sequence through container
  → container generates orders → SimulatedExchange fills them
  → results: P&L, trades, equity curve, drawdown, Sharpe
  → strategy signals (entry/exit only) persisted to DB for graph overlay
  → capital ± P&L returned to workbook.available_capital
  → results persisted permanently under this workbook
```

**Manual Paper Trade:**
```
User opens workbook, picks: stock=INFY, strategy=VWAP Reversion,
  capital=₹50,000, clicks START
  → ₹50,000 deducted from workbook.available_capital
  → Creates a TradingContainer (mode=PAPER) outside routing algo
  → Connects to live market data feed for INFY
  → Runs strategy on live data, fills are simulated
  → Strategy signals persisted to DB for graph overlay
  → User can watch signals and simulated performance in real time
  → User can stop at any time
  → on stop: capital ± P&L returned to workbook.available_capital
```

Same `TradingContainer` class as routing algo uses. Different runner, same code.
All results stored permanently under the workbook — reopening the workbook shows full history.

---

### 4.12 API Layer

All endpoints require authentication (JWT). All workbook endpoints enforce ownership (user sees only their workbooks; admin sees all).

```
C++ HTTP / WebSocket API (framework TBD: Drogon or Crow)

Auth:
  POST /auth/register
                                → create account (username, password)
  POST /auth/login
                                → returns JWT token
  GET  /auth/me
                                → current user info

Workbooks:
  GET    /workbooks
                                → list user's workbooks
  POST   /workbooks
                                → create new workbook (name, initial capital)
  GET    /workbooks/{wid}
                                → workbook details (capital, status, activity summary)
  PATCH  /workbooks/{wid}
                                → update workbook (add capital, rename)
  DELETE /workbooks/{wid}
                                → soft-delete workbook (admin: hard-delete)

Runs (within a workbook):
  POST /workbooks/{wid}/runs/start
                                → start routing algo run (capital, stocks, routing_algo_id)
  POST /workbooks/{wid}/runs/{rid}/stop
                                → stop run, force-exit all positions
  GET  /workbooks/{wid}/runs/{rid}/status
                                → current run state, containers, capital, P&L
  GET  /workbooks/{wid}/runs
                                → list all runs (current + historical)

Containers (within a workbook):
  GET  /workbooks/{wid}/containers
                                → list all containers (active + historical)
  POST /workbooks/{wid}/containers/{cid}/kill
                                → kill specific container

Backtest / Paper Trade (within a workbook):
  POST /workbooks/{wid}/backtests/start
                                → manual backtest (stock, strategy, capital, date range)
  GET  /workbooks/{wid}/backtests/{id}
                                → backtest results
  POST /workbooks/{wid}/paper/start
                                → manual paper trade (stock, strategy, capital)
  POST /workbooks/{wid}/paper/{id}/stop
                                → stop manual paper trade

Charts (within a workbook):
  GET  /workbooks/{wid}/containers/{cid}/chart
                                → bar data + signals + fills + events for graph
    query params: from, to, include=bars,signals,fills,rejections,events

Data:
  GET  /strategies
                                → list available strategies
  GET  /routing-algos
                                → list available routing algorithms
  GET  /workbooks/{wid}/portfolio
                                → workbook capital, positions, P&L
  GET  /workbooks/{wid}/fills
                                → fill history for this workbook

Delete (within a workbook):
  DELETE /workbooks/{wid}/backtests/{id}
                                → soft-delete a backtest result
  DELETE /workbooks/{wid}/paper/{id}
                                → soft-delete a paper trade session
  DELETE /workbooks/{wid}/runs/{rid}
                                → soft-delete a completed run's history

Admin:
  GET    /admin/workbooks
                                → list all users' workbooks
  DELETE /admin/workbooks/{wid}
                                → hard-delete (permanent)
  PATCH  /admin/workbooks/{wid}
                                → modify any workbook
  POST   /admin/workbooks/{wid}/runs/start
                                → start run in another user's workbook (logged)

WebSocket (scoped to authenticated user):
  /ws/workbooks/{wid}/market
                                → live bar events for this workbook's active symbols
  /ws/workbooks/{wid}/containers
                                → container status updates, fills
  /ws/workbooks/{wid}/portfolio
                                → real-time P&L, capital
```

---

### 4.13 Persistence Layer

```
PostgreSQL (via libpq or pqxx):

Core tables:
  users                     → id, username, password_hash (bcrypt), role (USER|ADMIN),
                               is_active, created_at, last_login_at
  workbooks                 → id, user_id (FK), name, description, main_capital,
                               status (ACTIVE|PAUSED|ARCHIVED), deleted_at (soft-delete),
                               created_at, updated_at
  workbook_capital_events   → id, workbook_id (FK), event_type (INITIAL|TOP_UP|WITHDRAWAL|
                               ACTIVITY_BORROW|ACTIVITY_RETURN), amount, related_activity_id,
                               note, created_at

Activity tables (all reference workbook_id):
  runs                      → workbook_id, run config, routing_algo_id, borrowed_capital,
                               mode, start/end time, total P&L, status, deleted_at
  backtests                 → workbook_id, stock, strategy, capital, date range,
                               result summary, deleted_at
  paper_trades              → workbook_id, stock, strategy, capital, start/end time,
                               result summary, deleted_at
  containers                → workbook_id, run_id/backtest_id/paper_trade_id,
                               strategy, symbol, capital, mode, P&L, status

Trading tables (all reference workbook_id + container_id):
  orders                    → workbook_id, container_id, every order submitted
  fills                     → workbook_id, container_id, every fill received
  positions                 → workbook_id, container_id, snapshots per session

Signal logging tables (signals-only — logged when strategy generates OrderIntents):
  strategy_signals          → workbook_id, container_id, strategy_id, symbol_id,
                               bar_timestamp, signal_type (ENTRY|EXIT),
                               order_intents_json, indicators_snapshot_json
  routing_decisions         → workbook_id, run_id, routing_algo_id, symbol_id,
                               bar_timestamp, decision (CREATE|SKIP|UPGRADE|KILL),
                               score, reason
  risk_rejections           → workbook_id, container_id, order_intent_id,
                               rule_name, reason_detail, bar_timestamp
  container_events          → workbook_id, container_id, event_type
                               (CREATED|UPGRADED|EXITED|FORCE_STOPPED|KILLED_BY_ROUTING_ALGO),
                               from_mode, to_mode, capital_at_event, pnl_at_event, timestamp

Market data tables (shared across all workbooks — not workbook-scoped):
  bars                       → symbol_id, timestamp, resolution (ONE_MIN for now),
                               open, high, low, close, volume,
                               data_source (provider name), fetched_at
  symbol_data_coverage       → symbol_id, date, resolution, data_source
                               (tracks which data exists in DB to avoid re-fetching)
  instruments                → symbol metadata (refreshed periodically from provider)

System tables:
  strategies                 → registered strategies and versions
  routing_algos               → registered routing algos and versions

Redis:
  live_bars:{symbol}          → latest bar per symbol
  container_status:{id}       → fast status lookup
  workbook_capital:{wid}       → current available/allocated capital per workbook
  kill_switch                 → global kill flag (fast read by risk engine)
```

**Soft-delete convention:** Tables with `deleted_at` column use soft-delete. When `deleted_at IS NOT NULL`, the row is hidden from all queries and UI. Users can soft-delete their own backtest results, paper trade sessions, completed run history, or entire workbooks. Admin can hard-delete (actual row removal) for permanent cleanup.

---

### 4.14 Auth Layer

Basic username/password authentication. Designed to be simple now, extensible later (OAuth, SSO, etc.).

```
AuthService
  register(username, password)   → creates user, stores bcrypt hash in DB
  login(username, password)      → validates credentials, returns JWT token
  validateToken(jwt)              → extracts user_id and role from token
  currentUser(jwt)                → returns User { id, username, role }

User roles:
  USER   → can create/manage their own workbooks only
  ADMIN  → can view/manage all workbooks, hard-delete, continue work on any workbook

Token:
  JWT with user_id, role, expiry
  Expiry: configurable (e.g. 24 hours)
  Passed as: Authorization: Bearer <token> on every request

Password storage:
  bcrypt hash stored in users table
  No plaintext passwords ever stored or logged
```

**Where auth lives in the system:**
- Auth is enforced **only on Thread 4 (API layer)** — HTTP handlers validate JWT before dispatching
- Hot path (Thread 0), routing algo (Thread 1) never see auth — they work with `workbook_id` as their identity
- No auth logic touches the trading engine — clean separation

**Access rules:**
- Every API request (except `/auth/register` and `/auth/login`) requires a valid JWT
- Workbook endpoints: `workbook.user_id == authenticated_user.id` OR user is ADMIN
- Admin actions are logged with `performed_by: admin_user_id` for accountability

---

### 4.15 Workbook System

A **workbook** is the top-level organizational unit for a user's work. It is an isolated workspace with its own capital pool, activity history, and graphs.

```
Workbook
  id                  → UUID
  user_id             → owner
  name                → user-chosen label (e.g. "NSE Momentum Q3 2024")
  description         → optional notes
  main_capital        → total capital the user has put into this workbook (manually entered)
  status              → ACTIVE | PAUSED | ARCHIVED

WorkbookManager
  create(user_id, name, initial_capital) → WorkbookID
  addCapital(workbook_id, amount)              → updates main_capital and available_capital
  borrowCapital(workbook_id, amount)           → deducts from available, returns borrow_id
    called when starting a run, backtest, or paper trade
  returnCapital(workbook_id, borrow_id, final_amount) → returns capital ± P&L
    called when activity finishes
  softDelete(workbook_id)                       → sets deleted_at, hides from UI
  listWorkbooks(user_id)                        → all non-deleted workbooks for user
  getWorkbook(workbook_id)                      → full details including activity history
```

**Workbook lifecycle:**

```
User creates workbook with ₹10,00,000
  → workbook.main_capital = 10,00,000
  → workbook.available_capital = 10,00,000

User starts routing algo run with ₹5,00,000 from this workbook
  → workbook.available_capital -= 5,00,000  (now 5,00,000 available)
  → run borrows ₹5,00,000

User starts a manual backtest with ₹1,00,000 from same workbook
  → workbook.available_capital -= 1,00,000  (now 4,00,000 available)

Backtest completes: simulated P&L = +₹8,000
  → workbook.available_capital += 1,08,000  (now 5,08,000 available)

User later stops the routing algo run: P&L = -₹12,000
  → workbook.available_capital += 4,88,000  (now 9,96,000 available)

User adds more capital: ₹2,00,000
  → workbook.main_capital = 12,00,000
  → workbook.available_capital += 2,00,000  (now 11,96,000 available)

User reopens this workbook 6 months later:
  → all past runs, backtests, paper trades visible with full history and graphs
  → user can start new activities using available capital
```

**What a workbook contains:**
- All routing algo runs (current + past) with full container history
- All manual backtests with results
- All manual paper trades with results
- Per-container graphs with bar data, signals, fills, and annotations
- Capital event audit trail (every borrow, return, top-up)

---

### 4.16 Chart Data and Signal Logging

**Per-container chart** — each container in a workbook has a graph that shows:

```
Layers (all toggleable in frontend):
  1. Candlestick bars      → OHLCV from bars table
  2. Indicator overlays    → EMA, VWAP, RSI values from strategy_signals.indicators_snapshot
  3. Entry/exit markers    → buy/sell points from fills table
  4. Risk rejections       → red markers where RiskEngine blocked an order (from risk_rejections)
  5. Container mode badges → timestamps where mode changed (BACKTEST→PAPER→REAL) from container_events
  6. Routing algo decisions → when routing algo evaluated/scored this container (from routing_decisions)
```

**Signal logging policy — signals-only (not every bar):**
- `strategy_signals` is written only when strategy.onBar() generates one or more OrderIntents
- Bars where strategy decided "do nothing" are NOT logged
- Each logged signal includes an `indicators_snapshot_json` — a frozen copy of all indicator values at that moment (EMA, RSI, VWAP, etc.)
- This lets the frontend reconstruct indicator overlays on the chart at signal points

**Chart data API:**
```
GET /workbooks/{wid}/containers/{cid}/chart?from=2024-01-01&to=2024-03-01
    &include=bars,signals,fills,rejections,events

Returns:
{
  bars: [...],            // OHLCV candlestick data
  signals: [...],         // entry/exit signals with indicator snapshots
  fills: [...],           // actual filled orders (buy/sell markers)
  rejections: [...],      // risk engine blocks
  events: [...]           // container mode changes, kills
}
```

**Workbook-level summary graph:**
- X-axis: calendar days
- Y-axis: total workbook P&L (realized + unrealized)
- One line per container/activity (color-coded by strategy)
- Overall portfolio equity curve

---

## 5. Key Flows

### 5.1 Per-bar processing — Thread 0 and Thread 1

These are two separate threads. Thread 0 does the work; Thread 1 reads the result via the RoutingRing.

**Thread 0 (hot path) — runs for every BarEvent off the MarketDataRing:**
```
BarEvent{symbol=RELIANCE, ...} arrives from MarketDataRing

  [1] Update indicators for this symbol
      IndicatorLibrary.update(RELIANCE, bar)
        → updates all indicator instances keyed to RELIANCE
          (EMA, VWAP, RSI, etc.) — incremental, not recalculated from scratch

  [2] Dispatch to all containers watching this symbol
      containers = ContainerManager.getContainersForSymbol(RELIANCE)
      for each container in containers:        ← could be 0, 1, or 2 containers
        container.onBar(bar)
          → strategy.onBar(bar, portfolioView) → [OrderIntents]
          → RiskEngine.check(each intent)
          → ExecutionVenue.submit(approved)

  [3] Forward signal to Thread 1 via RoutingRing
      RoutingRing.write( RoutingSignal {
        symbol_id:      RELIANCE,
        bar:            bar,
        indicator_snapshot: IndicatorLibrary.snapshot(RELIANCE)
          ← compact struct with pre-computed values: { ema_fast, ema_slow, rsi, vwap, ... }
          ← Thread 1 reads these directly — does NOT recompute indicators itself
      })

  [4] Between bar cycles: drain CommandRing (from Thread 1)
      → process any CreateContainer / KillContainer commands
      → update ContainerManager dispatch map accordingly
```

**Thread 1 (routing algo) — reads RoutingRing independently:**
```
RoutingSignal{RELIANCE, bar, snapshot} arrives from RoutingRing

  RoutingAlgo.onBar(signal.symbol, signal.bar, signal.indicator_snapshot)
    → evaluate strategies on RELIANCE using pre-computed snapshot
    → manage containers for RELIANCE (create/upgrade/kill decisions)
    → manage capital allocation (from run's borrowed capital)
    → write any container commands back to CommandRing for Thread 0
```

Thread 1 never touches the IndicatorLibrary directly. It reads a frozen snapshot that Thread 0 already computed. No shared mutable state between threads.

### 5.2 Container order flow

```
BarEvent → Container.onBar()
  → Strategy.onBar() → [OrderIntent, ...]
  → if OrderIntents generated: log to strategy_signals (async, via PersistenceRing)
  → for each intent:
      RiskEngine.check() → APPROVED | REJECTED
        → if REJECTED: log to risk_rejections (async)
      ExecutionVenue.submit() → order_id
  → FillEvent received
      Portfolio.update()
      Strategy.onFill()
      if Strategy.shouldExit() → Container.exit()
        → forceExit if positions open
        → capital returned to run's pool
```

### 5.3 BACKTEST → PAPER → REAL upgrade

```
RoutingAlgo:
  createContainer(RELIANCE, EMA_CROSSOVER, capital=0, BACKTEST)
    → Container replays recent historical bars, SimulatedExchange
    → RoutingAlgo evaluates signals, simulated P&L, drawdown

    → satisfied → container.upgrade()    [to PAPER]
        → data source: HistoricalLoader → LiveMarketFeed
        → SimulatedExchange stays, no capital committed
        → strategy state preserved
        → container_events logged: UPGRADED (BACKTEST → PAPER)

    → satisfied → container.upgrade()    [to REAL]
        → ExecutionVenue: SimulatedExchange → BrokerGateway
        → allocated_capital = ₹2,00,000 (from run's pool)
        → mode = REAL, strategy state preserved
        → container_events logged: UPGRADED (PAPER → REAL)
```

### 5.4 User stop

```
User clicks STOP in UI
  → API validates JWT, checks workbook ownership
  → API → RunManager.stop()
  → KillSwitch activated (Redis flag + in-memory flag)
  → RiskEngine rejects all new OrderIntents
  → For each active container:
      container.forceExit(current_market_price)
        → submit market exit orders for open positions
        → wait for fills (timeout: configurable)
        → capital returned to run's pool
  → RoutingAlgo.stop()
  → Run marked STOPPED in DB
  → Run capital ± P&L returned to workbook's main capital
  → Final P&L snapshot persisted
  → UI notified via WebSocket
```

### 5.5 Workbook lifecycle flow

```
User registers → logs in → JWT issued

User creates workbook "NSE Test" with ₹10,00,000
  → workbook created in DB, main_capital = 10,00,000

User starts routing algo run with ₹5,00,000
  → workbook.available_capital -= 5,00,000
  → RunManager starts, routing algo receives ₹5,00,000
  → containers created, trades happen, signals logged

User starts manual backtest with ₹1,00,000 (in same workbook, while run is active)
  → workbook.available_capital -= 1,00,000
  → backtest runs, results persisted, capital returned ± P&L

User stops routing algo run
  → all containers force-exited, capital returned ± P&L to workbook

User closes browser, comes back next week
  → opens same workbook → sees full history: runs, backtests, P&L, graphs
  → can start new activities with available capital
```

---

## 6. How Extensibility Works in Practice

### Adding a new strategy

```
1. Create class MeanReversionV2 : public Strategy
2. Implement onBar(), onFill(), onOrderUpdate(), shouldExit()
3. Register: StrategyRegistry::register<MeanReversionV2>("mean_reversion_v2")
4. Done — available in UI, usable by any routing algo, usable in manual backtest/paper
```

### Adding a new routing algo

```
1. Create class AggressiveRouter : public RoutingAlgo
2. Implement onBar() with custom evaluation and allocation logic
3. Register: RoutingAlgoRegistry::register<AggressiveRouter>("aggressive_router")
4. Done — available in UI for user to select
```

### Adding a new data source (e.g. Upstox)

```
1. Create class UpstoxProvider : public DataProvider
2. Implement historicalLoader() with Upstox API calls
3. Implement liveFeed() with Upstox WebSocket
4. Register: DataSourceRegistry::register<UpstoxProvider>("upstox")
5. Done — switch to it by changing one config value, no engine changes
```

### Adding a new market (e.g. crypto)

```
1. Write BinanceFeed : public MarketDataFeed
2. Write BinanceGateway : public BrokerGateway
3. Add CRYPTO to market enum in Symbol domain type
4. Done — existing strategies that are market-agnostic work immediately
```

### Adding a new instrument type (e.g. NSE F&O)

1. Add FutureAttributes / OptionAttributes to Instrument domain type
2. Extend MarketDataFeed to handle F&O symbols
3. Add F&O margin rules to RiskEngine
4. Done — routing algo and strategies see F&O instruments as regular instruments

### Adding a new risk rule

1. Create class WeeklyLossLimitRule : public RiskRule
2. Implement check(OrderIntent, context) → APPROVED | REJECTED
3. Register in RiskEngine
4. Done — applies to every container, every strategy, every order

---

## 7. Module / Folder Structure

Two separate repos, adjacent to each other:

```
trading-platform-cpp/    ← C++ backend (this repo)
trading-platform-ui/     ← React / TypeScript frontend (separate repo)
```

**C++ backend:**

```
trading-platform-cpp/
├── domain/
│   ├── symbol.hpp
│   ├── instrument.hpp
│   ├── price.hpp
│   ├── quantity.hpp
│   ├── order.hpp
│   ├── fill.hpp
│   ├── position.hpp
│   ├── bar_resolution.hpp     ← BarResolution enum (ONE_MIN, FIVE_MIN, etc.)
│   ├── workbook.hpp           ← WorkbookID, Workbook types
│   ├── user.hpp               ← UserID, User, Role types
│   └── events.hpp
│
├── auth/
│   ├── auth_service.hpp/cpp   ← register, login, validate token
│   ├── jwt.hpp/cpp            ← JWT generation and validation
│   └── password_hash.hpp/cpp  ← bcrypt hashing
│
├── workbook/
│   ├── workbook_manager.hpp/cpp   ← create, addCapital, borrow, return, softDelete
│   └── workbook_capital.hpp/cpp   ← WorkbookCapital accounting logic
│
├── market_data/
│   ├── market_data_feed.hpp       ← abstract interface
│   ├── historical_loader.hpp      ← abstract interface
│   ├── data_provider.hpp          ← abstract interface (wraps feed + loader per vendor)
│   ├── data_source_registry.hpp/cpp   ← holds all providers, returns active one
│   ├── data_fetch_service.hpp/cpp     ← fill-on-demand with DB caching + rate limiting
│   ├── bar_synthesizer.hpp/cpp        ← synthesize higher resolutions (deferred impl)
│   ├── providers/
│   │   ├── csv_provider.hpp/cpp           ← first provider (CSV files for backtesting)
│   │   ├── (zerodha_provider.hpp/cpp)     ← when broker decided
│   │   └── (upstox_provider.hpp/cpp)      ← when added
│   ├── nse_live_feed.hpp/cpp
│   └── database_loader.hpp/cpp
│
├── indicators/
│   ├── indicator.hpp              ← abstract base
│   ├── indicator_library.hpp/cpp
│   ├── trend/                     ← grouped by type as the list grows
│   │   ├── ema.hpp/cpp
│   │   └── sma.hpp/cpp
│   ├── momentum/
│   │   └── rsi.hpp/cpp
│   ├── volatility/
│   │   ├── atr.hpp/cpp
│   │   └── bollinger_band.hpp/cpp
│   └── volume/
│       ├── vwap.hpp/cpp
│       └── rolling_high_low.hpp/cpp
│
├── strategies/
│   ├── strategy.hpp                    ← abstract base
│   ├── strategy_registry.hpp/cpp
│   ├── strategy_registrations.cpp      ← only file touched when adding a strategy
│   ├── trend_following/                ← grouped by type as the list grows
│   │   └── ema_crossover.hpp/cpp
│   ├── mean_reversion/
│   │   └── vwap_reversion.hpp/cpp
│   └── breakout/
│       └── opening_range_breakout.hpp/cpp
│
├── container/
│   ├── trading_container.hpp/cpp   ← core reuse unit
│   └── container_manager.hpp/cpp   ← tracks all active containers
│
├── routing/
│   ├── routing_algo.hpp                ← abstract base
│   ├── routing_algo_registry.hpp/cpp
│   └── default_router.hpp/cpp          ← first routing algo
│
├── portfolio/
│   ├── portfolio_ledger.hpp/cpp    ← per-workbook, per-activity
│   └── capital_manager.hpp/cpp
│
├── risk/
│   ├── risk_engine.hpp/cpp
│   ├── risk_rule.hpp                       ← abstract base
│   ├── max_position_rule.hpp/cpp
│   ├── daily_loss_limit_rule.hpp/cpp
│   ├── kill_switch_rule.hpp/cpp
│   ├── market_hours_rule.hpp/cpp
│   ├── duplicate_container_rule.hpp/cpp    ← per-workbook scoping
│   └── mis_squareoff_rule.hpp/cpp
│
├── execution/
│   ├── execution_venue.hpp             ← abstract interface
│   ├── simulated_exchange.hpp/cpp
│   ├── broker_gateway.hpp              ← abstract broker interface
│   └── (zerodha_gateway.hpp/cpp)       ← when broker is decided
│
├── backtest/
│   ├── backtest_runner.hpp/cpp
│   └── backtest_result.hpp
│
├── engine/
│   ├── run_manager.hpp/cpp     ← orchestrates entire run lifecycle
│   └── event_bus.hpp/cpp       ← routes events between components
│
├── api/
│   ├── http_server.hpp/cpp
│   ├── websocket_server.hpp/cpp
│   ├── auth_middleware.hpp/cpp   ← JWT validation on every request
│   └── handlers/
│       ├── auth_handler.hpp/cpp        ← register, login
│       ├── workbook_handler.hpp/cpp    ← CRUD workbooks, capital management
│       ├── run_handler.hpp/cpp
│       ├── backtest_handler.hpp/cpp
│       ├── paper_handler.hpp/cpp
│       ├── chart_handler.hpp/cpp       ← serves chart data (bars + signals + fills)
│       ├── portfolio_handler.hpp/cpp
│       └── admin_handler.hpp/cpp       ← admin-only endpoints
│
├── persistence/
│   ├── database.hpp/cpp
│   ├── repositories/
│   │   ├── user_repository.hpp/cpp
│   │   ├── workbook_repository.hpp/cpp
│   │   ├── run_repository.hpp/cpp
│   │   ├── container_repository.hpp/cpp
│   │   ├── order_repository.hpp/cpp
│   │   ├── fill_repository.hpp/cpp
│   │   ├── bar_repository.hpp/cpp
│   │   ├── signal_repository.hpp/cpp          ← strategy_signals, routing_decisions
│   │   ├── risk_rejection_repository.hpp/cpp
│   │   └── container_event_repository.hpp/cpp
│   └── migrations/
│       └── schema.sql
│
└── tests/
    ├── unit/
    │   ├── test_indicators.cpp
    │   ├── test_strategies.cpp
    │   ├── test_risk_engine.cpp
    │   ├── test_portfolio.cpp
    │   ├── test_workbook_capital.cpp
    │   └── test_auth.cpp
    ├── integration/
    │   ├── test_container_lifecycle.cpp
    │   ├── test_routing_algo.cpp
    │   └── test_workbook_lifecycle.cpp
    └── replay/
        └── test_backtest_determinism.cpp
```

**Frontend (separate repo):**

```
trading-platform-ui/
└── src/
    ├── pages/
    │   ├── Login.tsx
    │   ├── Register.tsx
    │   ├── WorkbookList.tsx        ← dashboard: list of user's workbooks
    │   ├── WorkbookView.tsx        ← single workbook: activities + graphs
    │   ├── Backtest.tsx
    │   └── PaperTrade.tsx
    └── components/
        ├── CapitalInput.tsx
        ├── StockSelector.tsx
        ├── ContainerCard.tsx
        ├── ContainerChart.tsx           ← per-container candlestick + signals graph
        ├── WorkbookSummaryChart.tsx     ← workbook-level P&L equity curve
        └── PnLChart.tsx
```

---

## 8. Build Roadmap

| Step | What | Why first |
|---|---|---|
| 1 | Domain types + Event model (incl. WorkbookID, BarResolution, UserID) | Foundation everything else builds on |
| 2 | DataProvider interface + DataSourceRegistry + CSVProvider | Data abstraction before any data loading |
| 3 | Indicator library (EMA, VWAP, RSI) — keyed by (symbol, type, params, resolution) | Shared dependency for all strategies |
| 4 | Strategy base + 3 strategies (with required_resolution in metadata) | First testable trading logic |
| 5 | Portfolio ledger (per-workbook) + Risk engine (per-workbook scoping) | Capital and safety before any order |
| 6 | Simulated execution venue | Enables paper trading and backtest |
| 7 | Trading Container | Core reuse unit, paper + real |
| 8 | Backtest runner | Validate strategies on historical data |
| 9 | Routing algo base + first routing algo | Orchestration and capital allocation |
| 10 | Workbook system + Auth (users, JWT, workbook capital) | Multi-user workbook isolation |
| 11 | Persistence (PostgreSQL) — all tables incl. signal logging | Audit trail, history, recovery |
| 12 | API layer + WebSocket (workbook-scoped, auth middleware) | Connect to frontend |
| 13 | DataFetchService (DB caching + API fallback + rate limiting) | Minimize third-party API calls |
| 14 | Chart data API + signal/event logging | Per-container graphs in frontend |
| 15 | Manual backtest / paper trade UI flows (within workbooks) | Independent UI feature |
| 16 | Delete functionality (soft-delete for users, hard-delete for admin) | Workbook management |
| 17 | Live market data feed (NSE) | Paper and live trading |
| 18 | Broker gateway (when broker decided) | Real money trading |

Each step produces something fully testable before the next begins.

---

## 9. What Never Changes After Initial Build

Once built, these are permanent contracts. New features are additive only:

| Component | Status |
|---|---|
| Domain types | Permanent — extend only (new enums, new fields) |
| Event model | Permanent — add new event types only |
| Strategy interface | Permanent — new strategies implement it |
| RoutingAlgo interface | Permanent — new algos implement it |
| TradingContainer | Permanent — paper/real/upgrade all handled |
| RiskEngine | Additive only — new rules registered |
| IndicatorLibrary | Additive only — new indicators added |
| ExecutionVenue interface | Permanent — new venues implement it |
| DataProvider interface | Permanent — new providers implement it |
| Workbook model | Permanent — core organizational unit |
| PortfolioLedger | Permanent — per-workbook scoping |
| Auth layer | Permanent — extensible (basic now, OAuth/SSO later) |
| BarResolution enum | Permanent — new resolutions added as enum values |

---

## 10. Key Design Decisions

**Routing algo does not call strategy directly.**
Routing algo creates and manages containers. Container calls strategy. Routing algo never holds a strategy reference — it holds container references. This keeps routing algo and strategy fully decoupled.

**Container is the reuse unit for paper and real.**
The only difference between paper and real is the execution venue pointer inside the container. Everything else — strategy, indicators, risk, portfolio accounting — is identical. Upgrade is done in-place with no state loss.

**Indicators are shared, not per-container.**
One `EMA(20)` instance per `(symbol, params, resolution)` across all containers watching that symbol at the same resolution. Computed once per bar. All containers and the routing algo read the same value.

**Capital flows: Workbook → Activity → Container.**
Workbook owns the capital pool. Each activity (routing algo run, manual backtest, manual paper trade) borrows capital from the workbook. Within a run, the routing algo distributes borrowed capital to containers. Capital ± P&L returns up the chain on completion. This two-level model prevents double-allocation at both the workbook and the run level.

**Kill switch is enforced inside RiskEngine in C++.**
Even if the API server or WebSocket crashes, the risk engine still blocks new orders because the kill switch is an in-memory flag read by every `RiskEngine.check()` call. Redis is a secondary persistence, not the primary enforcement.

**One strategy implementation runs everywhere.**
Backtest, paper, and live all use the same strategy class, same indicators, same container. The only variable is the execution venue and the source of market data. This eliminates backtest-live divergence.

**Workbooks are fully isolated.**
Two workbooks never share capital, positions, or state. A user can run the same strategy on the same stock in two different workbooks without conflict. DuplicateContainerRule is enforced per-workbook only.

**Data source is system-level, not per-workbook.**
All workbooks share the same active data provider. The DB cache is shared too — bars fetched for one workbook benefit all others. Data source selection is infrastructure, not user-level.

**BarResolution is future-proofed from day one.**
The `BarResolution` enum, the resolution field on `BarEvent`, and the resolution column on the `bars` DB table all exist from Phase 1 — always set to `ONE_MIN` initially. When multi-resolution support is needed, zero structural changes are required.

**Signal logging is signals-only, not every bar.**
`strategy_signals` are logged only when strategy generates OrderIntents. This keeps DB volume manageable (~15K–60K rows/day for 300 stocks) while providing all the data needed for chart signal overlays. Bars where strategy did nothing are not recorded.

**Soft-delete by default, hard-delete for admin only.**
Users can hide their own data (backtests, paper trades, runs, workbooks) via soft-delete. Actual deletion requires admin. This preserves audit trails for real-money activities while giving users control over their UI.
