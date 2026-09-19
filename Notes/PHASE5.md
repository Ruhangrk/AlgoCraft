# Phase 5 — Persistence, market-data cache, auth, API

ARCHITECTURE.md / IMPLEMENTATION.md describe **what** Phase 5 is. This file is **how we build it**, in order. For a file-by-file map of the code as built (flows + Mermaid), see **`Notes/CODEMAP.md`**.

Do not start a step until the previous step is done and tested. Do not fetch Upstox 1-min history until explicitly approved.

**Stores (decided):**

- **SQLite** — relational system of record (users, workbooks, runs, fills, coverage, live-fetch metadata).
- **RocksDB** — market-data warehouse. One key = one NSE **session** of bars for `(ticker, resolution)`.

**Not in this phase:** broker gateway, Postgres, Redis. Paper trading uses **live market WebSocket** (not the 30-min REST “today” blob). The today blob is for charts / “today so far” REST reads only.

**Threads:** Thread 0 / 1 never open SQLite or RocksDB. Thread 2 (persistence) is the only writer. API reads via repositories or snapshots.

---

## Market-data rules (locked)

RocksDB key: `{ticker}|{resolution}|{YYYY-MM-DD}`  
Example: `RELIANCE|1m|2026-09-11`  
Value: packed OHLCV for that session only (~375 1-min bars). Not JSON. Not CSV.

**Trading calendar, not civil dates.** Weekends and NSE holidays are not keys and are not “holes”. “Today” / “previous session” / range edges are IST **session** dates.

SQLite, one row per `(ticker, resolution)`:

| Field | Meaning |
|---|---|
| `first_date` / `last_date` | Contiguous **closed** sessions in RocksDB. No holes. **Never includes today.** |
| `live_date` | IST session date the live blob belongs to (must match “today” or be empty). |
| `last_fetched_at` | When today’s day-blob was last written. |

### Read path — `ensureDataAvailable(ticker, from, to, resolution)`

Clamp `to` to now. Split the request:

**A. Closed sessions in `[from, to]`** (everything strictly before today’s session):

1. Read coverage row.
2. Missing sessions = requested closed sessions minus `[first_date, last_date]`.
3. If the request sits **past** `last_date` or **before** `first_date`, also fill every session **in between** so the range stays one block (prefix, suffix, or gap after downtime).
4. Fetch missing sessions from the active provider, **chunked**: 1-min max **28 calendar days**, **one instrument per request**, ~30 req/min.
5. Empty-but-open sessions: still write a day-key (0 bars) so the range does not stall.
6. Vendor has no more history: stop left-extend; set `first_date` to the first day actually stored. Do not loop.
7. **Write RocksDB day-blob first, then update SQLite `first_date` / `last_date`.** Never the reverse (that would skip refetch and serve a missing key).

**B. If the request includes today’s session:**

1. Ignore the main range for that day. Today is never in `last_date`.
2. If `live_date != today` (midnight / first query): yesterday-or-previous-session is **closed**. Treat it as missing historical (step A) even if a live blob exists — full fetch seals it, then extend `last_date`. Clear or replace `live_date`.
3. If `live_date == today` and `now - last_fetched_at <= 30 min`: use the existing today blob.
4. Else: REST-fetch today, **overwrite** today’s RocksDB key, set `live_date = today`, `last_fetched_at = now`. Do not put today into `last_date`.

**C. Serve from RocksDB only** after A/B. Never trade or chart off a live Upstox REST response.

**Not used for paper:** container `on_bar` in PAPER mode comes from the **market WebSocket**, not this REST today blob. Stale last-30-min of the REST blob does not affect paper fills.

**Date roll:** civil “yesterday” is wrong on weekends. Promote the **previous NSE session**. Saturday is not a range key.

---

## Step order

Each step: implement → unit test → stop. Next step only after you approve.

**Progress (2026-09-17):** 5.1–5.8 done. 5.9 = `NullLiveFeed` stub only (no Upstox WS).
**5.10 code path:** `UpstoxProvider` + `POST /market-data/ensure` wired (token from `~/.config/upstox/config.json`).
Do not run multi-stock 1-min fetches until you intentionally start the server and call the script.

Helper: `python3 scripts/ensure_5_stocks.py` → AlgoCraft API → Upstox (5 DefaultRouter names).

### Step 5.1 — SQLite skeleton

- Add SQLite (e.g. amalgamated sqlite3 or a small CMake dep).
- DB file outside git (e.g. `data/algocraft.db`); path via config.
- Migration runner: ordered `schema_*.sql`, version table.
- Open in WAL mode. Connection(s) owned by persistence layer, not Thread 0.
- Tables for this step only: `schema_migrations`. Smoke: open, migrate, close.

**Done when:** process creates/opens the db and applies migrations idempotently.

### Step 5.2 — Coverage + RocksDB day-blobs (no Upstox)

- RocksDB directory outside git (e.g. `data/bars/`).
- `BarStore`: `put_session(ticker, resolution, date, bars)`, `get_session(...)`.
- SQLite `symbol_data_coverage` with `first_date`, `last_date`, `live_date`, `last_fetched_at`, `source`, `sessions`.
- `DataFetchService.ensureDataAvailable` against **CsvProvider** first: treat CSV as the “vendor”; copy missing sessions into RocksDB; update coverage with RocksDB-then-SQLite order.
- Session calendar helper: IST date, NSE regular days (start with weekday Mon–Fri minus a static holiday list or “day has bars in CSV”; refine later).
- Seed from existing `data/1min/*.csv` into RocksDB. Confirm `HistoricalDataLoader` used by backtest/run can read **through** `ensureDataAvailable` → RocksDB (CSV only on miss).

**Done when:** `./build/algocraft_engine run` and Phase 4 tests work with bars served from RocksDB after first ingest; second run does not re-read CSV for days already stored. No Upstox.

### Step 5.3 — Today vs closed-session coverage logic

- Implement the locked read path (sections A/B/C) with a fake clock in tests.
- Cases to test:
  - request inside range → no vendor call
  - request extends right/left → fill sessions in between, range grows, still contiguous
  - request includes today, `live_date` stale vs 30 min → refetch today only
  - `live_date` is previous session after “midnight” → seal previous session into range, start new live
  - weekend: do not insert Saturday; Monday seals Friday
  - crash order: SQLite range never points at a missing RocksDB key
  - empty session still stored
  - vendor empty on left-extend → `first_date` stops

**Done when:** those tests pass. Still CSV-backed. No Upstox.

### Step 5.4 — Persist workbook / run / fills (in-memory engine → SQLite)

Schema (all activity tables have `workbook_id`; soft-delete `deleted_at` where listed in IMPLEMENTATION.md):

- `users`, `workbooks`, `workbook_capital_events`
- `runs`, `backtests`, `paper_trades`, `containers`
- `orders`, `fills`, `positions`
- `routing_decisions`, `container_events`
- `strategies`, `routing_algos` (registry snapshot)

Repositories write from `RunManager` **after** the run (or via PersistenceRing if already easy). Do not insert SQL in `on_bar`.

**Done when:** `./build/algocraft_engine run` leaves a workbook, run row, container rows, fills; restarting the process can read them back. Capital events match borrow/return.

### Step 5.5 — Signal + rejection logging

- `strategy_signals` — only when `on_bar` produced OrderIntents; include `indicators_snapshot_json`
- `risk_rejections` — rule name + reason
- Async: enqueue on Thread 0 (~copy), Thread 2 writes SQLite

**Done when:** a run produces signal rows only on intent bars, not every minute; rejections persist when risk blocks.

### Step 5.6 — Auth

- `AuthService`: register, login, bcrypt hash, JWT (user_id, role, expiry)
- USER vs ADMIN as in ARCHITECTURE.md
- No auth in the engine/hot path

**Done when:** unit tests for register/login/bad password/expired token.

### Step 5.7 — HTTP API (workbook-scoped)

Framework TBD at this step (Drogon or Crow). JWT on every route except `/auth/*`. Ownership: `workbook.user_id == user` or ADMIN.

First slice (must exist before UI):

- `POST /auth/register`, `POST /auth/login`, `GET /auth/me`
- `GET/POST /workbooks`, `GET/PATCH/DELETE /workbooks/{wid}`
- `POST /workbooks/{wid}/runs/start` — body is `RunConfig` (stocks, strategies, capital, from/to, router). Replaces hardcoded `main.cpp` universe.
- `POST .../runs/{rid}/stop`, `GET .../runs/{rid}/status`, `GET .../runs`
- `GET /workbooks/{wid}/containers`, `GET /workbooks/{wid}/fills`, `GET /workbooks/{wid}/portfolio`
- `GET /strategies`, `GET /routing-algos`

Then: manual backtest/paper start-stop, chart `GET .../containers/{cid}/chart`, soft-delete, admin list/hard-delete.

**Done when:** curl/JWT can create a workbook, start a CSV-backed run, read fills/status; `main.cpp` is optional CLI only.

### Step 5.8 — UI WebSockets (status)

- `/ws/workbooks/{wid}/portfolio`
- `/ws/workbooks/{wid}/containers`

Push snapshots already published by the engine. Not market data.

**Done when:** a test client receives P&L/container updates during a run.

### Step 5.9 — Paper live market WebSocket

- PAPER containers subscribe to the active provider **live feed** (Upstox market WS when that provider exists; not REST candles).
- Execution stays `SimulatedExchange`.
- Historical warmup / charts still go through DataFetchService → RocksDB.
- Do **not** drive paper `on_bar` from the 30-min today blob.

Upstox live feed is a **new DataProvider** (originally Phase 9). In this step: interface + one provider; still no broker orders. Smoke tests: 1–2 symbols, short connect, **no** multi-month 1-min REST.

**Done when:** a paper container receives live bars over WS and can simulate fills; stop returns capital. Historical cache rules unchanged.

### Step 5.10 — Optional: Upstox REST into DataFetchService - skip this we will not implement this 

Only after explicit approval (1-min window ≤ 28 calendar days, one stock per request).

- `UpstoxProvider.historicalLoader` fills **missing sessions only**.
- Same coverage rules as 5.3.
- Never refetch a sealed day in the main range.

**Done when:** a gap fetch writes RocksDB + coverage; a second `ensureDataAvailable` for the same range hits RocksDB only.

---

## Out of scope (do not sneak in)

- Postgres, Redis
- Broker order gateway
- Thread 0/1 talking to disk
- DefaultRouter BACKTEST → PAPER → REAL ladder (Phase 4 shortcut stays)
- Fetching months of 1-min without approval

---

## Definition of done (Phase 5)

- SQLite holds users, workbooks, runs, fills, signals, coverage; restart does not lose them
- RocksDB holds session blobs; coverage never lists a day without a key
- Closed range is contiguous sessions; today is live + `live_date` + 30-min REST refresh
- Paper uses live WS; REST today blob is not the paper tape
- Auth + workbook API can start/stop a run without editing `main.cpp`
- Persistence never blocks the hot path
- Soft-delete on user-facing activity rows

When this file’s steps are all checked, IMPLEMENTATION.md Phase 5 is complete under SQLite + RocksDB instead of PostgreSQL + `bars` SQL table.
