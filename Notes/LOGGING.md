# AlgoCraft logging (locked 2026-09-28)

## Model

- **Facade:** `AC_LOG_TRACE|DEBUG|INFO|WARN|ERROR` → `algocraft::log::write` (`include/algocraft/log/log.hpp`)
- **Async drain:** `LogHub` queue → **PersistenceService (T2)** via `drain_once()` on serve/run/backtest; other CLI cmds still use a dedicated `log_drain` thread
- **Hot-path SPSC:** `AsyncLogger` + `LogEvent` remains for Phase0 / future single-producer T0 ring (polled by T2)
- **DB signal loggers** (`strategy_signals`, etc.) are separate — not console logging

## Levels (CLI)

Any command accepts a level token anywhere after the binary name:

```text
./build/algocraft_engine serve debug
./build/algocraft_engine serve data/1min 8080 trace
./build/algocraft_engine run info
scripts/serve debug
scripts/serve --restart trace
```

| Level | Shows |
|-------|--------|
| `error` | failures only |
| `warn` | + warnings (auth fail, feed errors, borrow fail, queue full) |
| `info` | + lifecycle (default): start/stop run, workbook CRUD, API listen, persist begin/done, router winners |
| `debug` | + decisions: signals, risk reject, fills, router eval/create, ensure_data, container kill/upgrade |
| `trace` | + major function flow: `container_on_bar`, `containers_on_bar`, run/live detail, eval empty skip |

## Where logs are wired (application)

| Area | Examples |
|------|----------|
| `main` / CLI | start/exit, db smoke, ingest, backtest/run summaries, serve |
| API | HTTP line, workbook create/delete/capital, run start/stop, backtest, market ensure |
| Auth | register ok, login ok/fail |
| Engine | `RunManager`, `LiveRunService` start/stop/feed |
| Routing | default + top15 eval/create/winners |
| Containers | create/warmup/start/kill/on_bar/signal/reject/fill/exit |
| Market data | `ensure_data`, Upstox/virtual feed warn/error |
| Persistence | `persist_run` begin/done/rollback |
| Phase0 | persist ring drain via `AC_LOG_INFO` |

**Sink only (keep raw spdlog):** `src/log/log.cpp`, `src/log/log_hub.cpp`.

## Rules

1. Application code uses `AC_LOG_*` — not raw `spdlog::` (except log sink).
2. Do not block the caller on log I/O — hub enqueue never waits.
3. Fixed-size messages (`LogEvent` 256 chars); long lines truncate.
4. Queue full → drop + periodic `warn` with `dropped_total`.
5. When real T2 lands: move `LogHub::drain_available` into the persistence loop; keep the same facade. **Do not leave a separate `log_drain` forever.**
6. Trace is for short debug sessions — can flood the ring under multi-symbol live.

## Status (2026-09-28)

**Complete for current scope:** facade, hub, CLI levels, **daily log files** (`data/logs/algocraft_YYYY-MM-DD.log`), app `spdlog::` migrated, major paths instrumented.

**Not in this pass (by design):** per-strategy `on_bar` spam, merging drain into real T2 (T2 not stood up yet).

---

## Where to look

- **Terminal:** stdout/stderr of `serve` / CLI (same as before).
- **Daily files:** `data/logs/algocraft_YYYY-MM-DD.log` (local midnight rollover). Created by `log::init_sinks` at process start. Both sinks receive every line the drain thread emits.

```text
data/logs/algocraft_2026-09-28.log
```
