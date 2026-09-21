# Joint roadmap — AlgoCraft + AlgoCraft-UI

**Audience:** Cascade / agents working in either repo.  
**Companion docs:** UI `ARCHITECTURE.md`; engine `Notes/CODEMAP.md`, `ARCHITECTURE.md`, `PHASE5.md`, `UPSTOX.md`.

**Do not create new files when not needed** — for both repos. Prefer extending existing modules (`api/*_routes`, `endpoints.ts`, pages, repositories, migrations). Add a file only when it owns a clear boundary (new route group, new page, new repository, new migration) and keeps code length / ownership clean. If an edit fits an existing file without bloating it, edit that file.

**How to work:** do **one micro-step at a time** (B0a, S1a, …). Backend first → tests/smoke → stop for approval → then frontend for that slice if needed.

---

## 0. Current baseline (read this before coding)

### Backend (AlgoCraft) — as of Crow refactor

HTTP is **Crow**, split across route modules:

| Module | Paths |
|---|---|
| `api/auth_routes.*` | `POST /auth/register`, `POST /auth/login`, `GET /auth/me` |
| `api/market_routes.*` | `GET /strategies`, `GET /routing-algos`, `POST /market-data/ensure` |
| `api/workbook_routes.*` | `GET/POST /workbooks`, `POST /workbooks/{wid}/runs/start`, `GET .../runs\|fills\|portfolio\|containers` |
| | **Real** WS: `/ws/workbooks/{wid}/portfolio`, `/ws/workbooks/{wid}/containers` |
| `api/http_helpers.*` | CORS (`*`), JWT gate, JSON helpers |

Still in engine but **not HTTP-exposed:** signals/rejections persist post-run; no list API yet. Manual backtests: `POST/GET .../backtests*` (S3c).

**Known backend bugs / gaps:**

| Issue | Notes |
|---|---|
| Soft-delete APIs | Some `deleted_at` columns exist; no DELETE routes yet → S4. |
| Phase 5.9 live paper tape | `NullLiveFeed` only — **parked** (not in S1–S5). |

**Dual market-data paths (locked):**

| Path | Resolutions | Used by |
|---|---|---|
| **Engine** | **1m only** | strategies, indicators, backtests, runs, routing |
| **Charts** | **1d, 1w, 1M** (no yearly) | frontend graphs only via OHLCV API → Rocks |

Do **not** drive strategy `on_bar` from D/W/M. Do **not** add yearly anywhere.

### Frontend (AlgoCraft-UI) — U0–U3 done

Auth, workbook list/create, sync routing run, runs/containers/fills. Paths via `src/api/endpoints.ts`.

Local `workbookCache` — **re-verify `GET /workbooks` after Crow + `WorkbookRepository`; remove cache if list is correct.**

### Hard constraints (both repos)

- Money: **paise**. Time: **ns** internally; ISO dates only at API query edges where noted.
- Upstox: **1 instrument / request**; **1m ≤ ~28 calendar days** per request; ~**30 req/min**. Never blast full-NSE 1m history.
- UI = display + commands. Capital / risk / costs live in C++.
- All market reads for charts/backtests go through **`DataFetchService` → Rocks + coverage**, never raw vendor from the UI.

### When you must ask the user for OK (Upstox)

| Need explicit OK | Do **not** need OK |
|---|---|
| Multi-symbol **1m** jobs / CLI backfills | Single-ticker on-demand **1d / 1w / 1M** chart miss (normal UI) |
| **1m** windows near/at the 28-day cap (or larger chunking jobs) | Smoke tests (1–2 days / coarse intervals) already allowed by `UPSTOX.md` |
| Any “warm the vault” of 1m for many names | |

---

## Backend Engineering Rules (AlgoCraft)

1. **Layers:** `*_routes` = auth + parse + call service/repo + JSON. **No `sqlite3_*` in routes.** New SQL → repository. Multi-step orchestration → small service/helper (not a fat Crow lambda).
2. **Files:** prefer extend `market_routes` / `workbook_routes` / existing repos. New `*_routes` / `*_repository` only when a file would become hard to navigate.
3. **No bar aggregation:** never synthesize one TF from another. Fetch the Upstox unit the client asked for, then cache.
4. **No prewarm:** on-demand ensure + Rocks cache. No full-universe ensure of any TF on startup.
5. **Rocks keying:** one key = one packed blob `(ticker, resolution, period)`. **1m** = session `{ticker}\|1m\|YYYY-MM-DD`. **1d/1w/1M** = prefer year (or month) blobs e.g. `{ticker}\|1d\|YYYY`. Never one-bar-per-key.
6. **Rocks size:** D/W/M are tiny. Risk is unbounded **1m** (≈4–5 MB/ticker/year). Keep 1m on-demand and bounded.
7. **Crow:** single-threaded `run()` (one SQLite handle). CORS as today.
8. **Test gate:** each micro-step ends with unit test and/or `serve` smoke before Progress log = done.
9. **No BarSynthesizer** in this roadmap.

**Upstox window caps** (enforce in code; see `Notes/UPSTOX.md` + official V3 docs):

| Unit | Interval | Official max / request | **AlgoCraft chunk (use this)** |
|---|---|---|---|
| minutes | 1–15 | 1 month | **28 calendar days** |
| minutes | 16–300 | 1 quarter | **~85 calendar days** (under one quarter) |
| hours | 1–5 | 1 quarter | **~85 calendar days** |
| days | 1 | 1 decade | **9 years** (safer than full decade) |
| weeks | 1 | No documented limit | On-demand; still 1 instrument; ≤~30 req/min; no full-universe warm |
| months | 1 | No documented limit | Same as weeks |

Exceeding the official max → API error. Always chunk. **No aggregation** across units.

---

## Step plan (do in order)

Each micro-step: **implement → test/smoke → stop for approval.**  
Product steps (A0, S1…) may span frontend; **backend micro-steps (B0a, S1a…)** are sized for one Cursor agent turn.

---

### Step A0 — Adopt API surface in the UI (no new product features)

**Goal:** Frontend matches Crow API as shipped; drop dead workarounds.

| | Work |
|---|---|
| **Backend** | Optional: document WS auth (`Authorization: Bearer` or `?token=`) in CODEMAP. Prefer doing that in **B0a**. |
| **Frontend** | Diff `endpoints.ts` + types vs Crow. Fix DTO drift. Real WebSocket URLs. Re-test list-after-create; delete `workbookCache` if obsolete. |

**Done when:** Login → create workbook → list shows it → start small run → tables fill; WS connects if used.

---

### Step B0 — Backend hygiene (before S1)

#### B0a — Docs: WS auth + known gaps

| | Work |
|---|---|
| **Backend** | CODEMAP: WS auth (`Bearer` / `?token=`). Note runs/start wid bind bug until B0b. |

**Done when:** CODEMAP states WS auth and B0b bug clearly.

#### B0b — Bind `runs/start` to path workbook id

| | Work |
|---|---|
| **Backend** | `POST /workbooks/{wid}/runs/start` must persist/list under **that** `wid`. Load name/capital from `WorkbookRepository`; fix `RunManager` / persist path so `workbook_id` in response equals path `wid`. Stable DTO: `run_id`, `workbook_id`, selected, skipped, fills, … |

**Done when:** create workbook → start run → `GET /workbooks/{wid}/runs` returns that run.

**Files (prefer):** `workbook_routes.cpp`, `run_manager.*`, `activity_repository.*` as needed — minimal change.

---

### Step S1 — Full NSE stock universe

**Product:** Search/select any NSE equity. **Do not** fetch 1m for all symbols.

#### S1a — Migration: `instruments` table

| | Work |
|---|---|
| **Backend** | New `migrations/schema_00N.sql`: `instruments` (ticker PK, name, isin, exchange, segment, lot_size, tick_size_paise, instrument_key, active, updated_at). |

**Done when:** migrate smoke creates table.

#### S1b — Ingest CLI + `InstrumentRepository`

| | Work |
|---|---|
| **Backend** | CLI (or `algocraft_engine` subcommand) downloads/parses Upstox `complete.csv.gz` (see `UPSTOX.md`); filter **NSE EQ** cash; upsert via `InstrumentRepository`. |

**Done when:** after ingest, SQLite has thousands of NSE EQ rows; re-run is idempotent.

#### S1c — HTTP search/detail

| | Work |
|---|---|
| **Backend** | `GET /instruments?q=&limit=` (cap e.g. 50); `GET /instruments/{ticker}`. Extend `market_routes` or add `instrument_routes` only if needed. |
| **Frontend** | (after S1c) `InstrumentSearch` combobox. |

**Done when:** `q=RELI` returns RELIANCE; unknown ticker → 404; selection needs no local bars.

---

### Step S2 — Stock charts D / W / M (chart path only)

**Product:** Daily / Weekly / Monthly graphs. **No yearly** in API or UI.  
**Engine stays on 1m.** Chart TF is storage + OHLCV API only.

#### S2a — Resolution + Rocks keying for chart TFs

| | Work |
|---|---|
| **Backend** | Support chart resolutions `1d` / `1w` / `1M` in domain/store (add month if missing). Key scheme: year/month blobs for chart TFs; keep 1m session keys. Coverage rows per `(ticker, resolution)`. **No strategy wiring.** |

**Done when:** unit tests pack/write/read a sample `1d` blob; 1m path unchanged.

#### S2b — Upstox fetch for days / weeks / months

| | Work |
|---|---|
| **Backend** | Extend `UpstoxProvider` historical loader for `days` / `weeks` / `months` units; enforce window caps; one instrument per request. |

**Done when:** smoke fetch 1 ticker daily for a short range (token required); no aggregation code.

#### S2c — `DataFetchService` ensure for chart resolutions

| | Work |
|---|---|
| **Backend** | `ensure_data_available` for `1d`/`1w`/`1M`: miss → vendor → Rocks → coverage. On-demand only. |

**Done when:** second ensure for same range does not call vendor (vendor_fetches unchanged).

#### S2d — OHLCV HTTP API

| | Work |
|---|---|
| **Backend** | `GET /instruments/{ticker}/ohlcv?resolution=1d\|1w\|1M&from=&to=` (optional short `1m` later). Map to ensure + read Rocks. |
| **Frontend** | (after S2d) Stock detail tabs D/W/M only — **no yearly tab**. |

**Done when:** Pick INFY → D/W/M candles load; cold miss may hit Upstox once; warm hit is Rocks-only.

---

### Step S3 — Manual backtest (still **1m** engine data)

**Product:** Strategy × stock × range → **% return after costs**. Uses **1m** bars via existing engine path.

#### S3a — Migration: `backtests` table

| | Work |
|---|---|
| **Backend** | `backtests` table (workbook_id, strategy, ticker, capital, range, pnl, fees, return_pct, …, deleted_at). |

**Done when:** migrate smoke OK.

#### S3b — Persist helper around `BacktestRunner`

| | Work |
|---|---|
| **Backend** | Thin helper/service: ensure **1m** range (chunked; ask OK if long/multi) → `BacktestRunner` → write `backtests` (+ fills/signals as designed). Borrow/return workbook capital. |

**Done when:** unit/integration: known CSV/Rocks range → deterministic pnl/fees/return_pct row.

#### S3c — Backtest HTTP

| | Work |
|---|---|
| **Backend** | `POST /workbooks/{wid}/backtests/start`, `GET .../backtests`, `GET .../backtests/{id}`. Sync first. |
| **Frontend** | (after S3c) launcher + big % return card. |

**Done when:** EMA × RELIANCE × known range matches CLI; fees non-zero when trades happen.

---

### Step S4 — History organization

#### S4a — Soft-delete + filtered list APIs

| | Work |
|---|---|
| **Backend** | `DELETE` soft-delete for runs/backtests; list filters `type`, date, limit/cursor; honor `deleted_at`. |

**Done when:** soft-deleted rows hidden from default lists; admin/raw DB still has them.

#### S4b — Frontend hub (after S4a)

| | Work |
|---|---|
| **Frontend** | Tabs Runs \| Backtests; filters; no folder inventing yet. |

---

### Step S5 — Event timeline

#### S5a — Read APIs over existing signal/rejection/fill rows

| | Work |
|---|---|
| **Backend** | `GET .../runs/{rid}/events` (or per-container) for signals, rejections, fills first. |

**Done when:** after a run, timeline JSON shows entries without reading SQLite by hand.

#### S5b — Lifecycle / routing events (only if missing)

| | Work |
|---|---|
| **Backend** | Add persistence + API for container lifecycle / routing decisions **only if** not already stored. |
| **Frontend** | Timeline UI + toggles. |

---

## Suggested overall sequence

```
A0     UI adopts Crow API
B0a    Docs (WS auth + gaps)
B0b    Fix runs/start → path wid
S1a    instruments migration
S1b    ingest CLI + repository
S1c    GET /instruments
S2a    chart TF keys/coverage (no strategy change)
S2b    Upstox days/weeks/months fetch
S2c    ensure for chart TFs
S2d    GET .../ohlcv
S3a    backtests migration
S3b    BacktestRunner persist helper (1m)
S3c    backtest HTTP
S4a    soft-delete + filters
S4b    UI hub
S5a    events read API
S5b    extra event types if needed
```

Optional later: paper trade, Phase 5.9 live feed, async runs + STOP, admin, AI agent.

---

## Architecture rules (both repos)

1. **Open/closed:** new strategies/routers/instruments via registry + data — not special-case UI logic.  
2. **One capital model:** workbook → activity → container; UI never invents balances.  
3. **Fetch once, reuse forever:** charts/backtests go through `DataFetchService` + Rocks/SQLite.  
4. **API ownership:** auth / market / workbook(+activity) route modules stay separate; grow a new `*_routes` file only when an existing module would become hard to navigate.  
5. **UI ownership:** `api/` + `types/` mirror backend DTOs; pages compose features; no trading math in React.  
6. **Do not create new files when not needed** — prefer edits; new files need clear ownership.  
7. **Engine = 1m; charts = D/W/M only** — do not mix paths.

---

## Cross-repo checklist per micro-step

- [ ] Backend: migration (if any) + code + unit/smoke  
- [ ] Postman updated when HTTP changes (`postman/`)  
- [ ] Progress log: mark micro-step done with date  
- [ ] Frontend only when that slice’s API exists  
- [ ] Manual smoke: UI ↔ `serve` on `:8080` when UI touched  
- [ ] No full-universe ensure; no unbounded 1m without OK  

---

## Locked decisions

| # | Topic | Decision |
|---|---|---|
| 1 | Instrument source | Upstox `complete.csv.gz` → SQLite via CLI; NSE EQ filter v1 |
| 2 | Chart TFs | Fetch `days` / `weeks` / `months` from Upstox; **never aggregate**; **no yearly** |
| 3 | Engine TF | **1m only** for strategies/backtests/runs until explicitly changed later |
| 4 | Prewarm | **None** — on-demand + Rocks cache |
| 5 | Backtest | Sync HTTP first |
| 6 | Vite proxy vs CORS | Keep `/api` proxy for now; both valid |
| 7 | Soft-delete UI | After S4a backend |
| 8 | Phase 5.9 live tape | Parked |

---

## Progress log

| Step | Status | Date | Notes |
|---|---|---|---|
| A0 | Pending | | UI adopt Crow |
| B0a | Done | 2026-09-21 | CODEMAP: WS auth Bearer/`?token=`; gaps table; Crow modules |
| B0b | Done | 2026-09-21 | `adopt` + `existing_workbook_id`; runs/start binds path `wid` |
| S1a | Done | 2026-09-21 | `schema_005.sql` instruments table + indexes |
| S1b | Done | 2026-09-21 | InstrumentRepository + `instruments ingest`; NSE_EQ EQUITY INE filter |
| S1c | Done | 2026-09-21 | GET /instruments?q=&limit= + GET /instruments/{ticker} |
| S2a | Done | 2026-09-21 | OneMonth + year blob keys for 1d/1w/1M; 1m session keys unchanged |
| S2b | Done | 2026-09-21 | Upstox days/weeks/months + chunk caps; short daily smoke |
| S2c | Done | 2026-09-21 | ensure_chart_available year blobs; second ensure no vendor |
| S2d | Done | 2026-09-21 | GET /instruments/{ticker}/ohlcv (1d/1w/1M) |
| S3a | Done | 2026-09-21 | `schema_006.sql` backtests table |
| S3b | Done | 2026-09-21 | BacktestService ensure+run+persist+capital return |
| S3c | Done | 2026-09-21 | POST/GET /workbooks/{wid}/backtests* |
| S4a | Pending | | soft-delete + filters |
| S4b | Pending | | UI hub |
| S5a | Pending | | events read API |
| S5b | Pending | | extra events if needed |
