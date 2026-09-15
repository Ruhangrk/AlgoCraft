# AlgoCraft

C++ NSE algorithmic trading engine.

**Phase 0** skeleton. **Phase 1** domain types. **Phase 2** indicators, 3 strategies, CSV backtest on RELIANCE / INFY / TCS (28-day 1-min window).

## Build

Requires CMake 3.24+, a C++20 compiler, and network on first configure (FetchContent: GoogleTest, spdlog, Boost.Lockfree).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DALGOCRAFT_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/algocraft_engine
./build/algocraft_engine backtest
```

`backtest` runs all 3 strategies on `data/1min/{RELIANCE,INFY,TCS}.csv` (1-min bars, 2026-08-18 to 2026-09-11, fetched from Upstox offline). Default `./build/algocraft_engine` is the Phase 0 ring-buffer smoke.

## Layout

- `include/algocraft/` public headers
- `src/` implementations
- `data/1min/` NSE 1-minute CSVs used by Phase 2
- `tests/unit/` GoogleTest
- `Notes/` architecture and implementation plan
