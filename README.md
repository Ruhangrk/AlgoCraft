# AlgoCraft

C++ trading engine for NSE equities. One strategy implementation runs across backtest, paper, and live. Capital, risk, and execution live in the engine; the companion UI (`AlgoCraft-UI`) is display and control only.

HTTP API is Crow (default `8080`). Market data is cached in RocksDB (bars) with SQLite as the relational store (users, workbooks, runs, coverage). Optional Upstox REST/WebSocket for live and historical fills.

## Requirements

- CMake 3.24+, C++20 compiler (GCC/Clang)
- System packages: `librocksdb-dev`, OpenSSL, libcurl, zlib, pkg-config
- Network on first configure (FetchContent pulls GoogleTest, spdlog, Boost.Lockfree, SQLite amalgamation, ASIO, Crow)

Optional for vendor data / live feed:

- `~/.config/upstox/config.json` (mode `600`) with `upstox_access_token` — never commit this file

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Useful options:

| Flag | Default | Notes |
|------|---------|--------|
| `ALGOCRAFT_BUILD_TESTS` | `ON` | Unit tests under `tests/unit/` |
| `ALGOCRAFT_ENABLE_SANITIZERS` | `OFF` | ASan + UBSan |

Keep build parallelism at `-j2` on typical developer machines to avoid RAM pressure.

## Commands

Binary: `./build/algocraft_engine`

```text
algocraft_engine <serve|run|backtest|db|instruments> [args...] [log_level]
```

Log level may appear anywhere after the binary name: `error`, `warn`, `info` (default), `debug`, `trace`.

### API server

Prefer the wrapper (single-instance, lock file under `data/`):

```bash
scripts/serve                 # info
scripts/serve debug
scripts/serve --restart       # kill existing serve, then start
scripts/serve --restart trace
```

Equivalent direct invoke:

```bash
./build/algocraft_engine serve [data_dir] [port] [log_level]
# defaults: data/1min, 8080
```

After promoting/activating agent strategies, `POST /agent/strategies/activate` schedules:

```bash
scripts/reload_engine [log_level]   # rebuild algocraft_engine, then serve --restart
```

### Offline / CLI workflows

```bash
# Phase-4 style routing run (CSV/Rocks-backed universe)
./build/algocraft_engine run [data_dir]

# Phase-2 backtests on RELIANCE / INFY / TCS CSVs
./build/algocraft_engine backtest [data_dir]
./build/algocraft_engine backtest consecutive_up_clip [data_dir]

# SQLite smoke
./build/algocraft_engine db [db_path]          # default: data/algocraft.db

# Ingest NSE EQ instruments (Upstox complete dump by default)
./build/algocraft_engine instruments ingest [source] [db_path]
```

### Helpers

```bash
# Ensure DefaultRouter sample stocks via HTTP (server must be running; uses /market-data/ensure)
python3 scripts/ensure_5_stocks.py [host] [port]

# Inspect RocksDB session blobs
python3 scripts/dump_rocks_session.py --help
```

## Data layout

| Path | Role |
|------|------|
| `data/1min/` | CSV 1-minute bars (offline / seed) |
| `data/algocraft.db` | SQLite (auth, workbooks, runs, coverage, …) |
| `data/bars/` | RocksDB bar warehouse |
| `data/logs/` | spdlog / reload logs |
| `migrations/` | SQLite schema migrations (applied on serve) |

Defaults are compile-time paths into the source tree (`ALGOCRAFT_DATA_DIR`, `ALGOCRAFT_DB_PATH`, `ALGOCRAFT_BARS_DIR`).

## API surface (high level)

| Area | Examples |
|------|----------|
| Auth | `POST /auth/register`, `POST /auth/login`, `GET /auth/me` |
| Catalog | `GET /strategies`, `GET /routing-algos` |
| Market | `POST /market-data/ensure` |
| Workbooks | CRUD, runs, backtests, fills, portfolio; SSE streams under `/workbooks/{id}/stream/*` |
| Agent | `/agent/strategies/compile|promote|activate` (JWT) |

Details: `Notes/JOINT_ROADMAP.md`, `Notes/AGENT_STRATEGY_APIS.md`.

## Environment

| Variable | Purpose |
|----------|---------|
| `ALGOCRAFT_LIVE_FEED` | `upstox` or `virtual` when anchoring live runs to IST today |
| `ALGOCRAFT_VIRTUAL_WS_URL` | Override URL for the virtual live feed |

## Repository map

```text
include/algocraft/   public headers
src/                 engine, API, strategies, persistence, market data
tests/unit/          GoogleTest
scripts/             serve, reload_engine, data helpers
Notes/               architecture, roadmap, Upstox, logging, strategy notes
```

Start with `Notes/CODEMAP.md` for how the running system is wired, and `Notes/ARCHITECTURE.md` for design intent.

## Notes for contributors

- Money is **paise**; timestamps are nanoseconds internally.
- Engine path uses **1-minute** bars only. Coarser resolutions are for charts, not strategy `on_bar`.
- Upstox: one instrument per request; 1-minute history max ~28 calendar days per call; roughly ~30 requests/minute. Do not blast full-NSE history.
- Token and secrets stay outside the repo.
