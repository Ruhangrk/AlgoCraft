# AlgoCraft

C++ NSE algorithmic trading engine.

**Phase 0** skeleton. **Phase 1** domain types. **Phase 2** indicators, strategies, CSV backtest. **Phase 3** workbook / container / risk. **Phase 4** DefaultRouter + RunManager (10 stocks × 3 strategies, 15-day BACKTEST, winners → REAL, ₹10 crore).

## Build

Requires CMake 3.24+, a C++20 compiler, and network on first configure (FetchContent: GoogleTest, spdlog, Boost.Lockfree).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DALGOCRAFT_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/algocraft_engine
./build/algocraft_engine backtest
./build/algocraft_engine run
```

`backtest` runs the 3 Phase-2 strategies on `data/1min/{RELIANCE,INFY,TCS}.csv`. `run` is Phase 4: 15-day window (2026-08-28 to 2026-09-11), 10 stocks × ema/vwap/orb, profitable pairs go straight to REAL with ₹10 crore split equally (no paper). Default `./build/algocraft_engine` is the Phase 0 ring-buffer smoke.

## Layout

- `include/algocraft/` public headers
- `src/` implementations
- `data/1min/` NSE 1-minute CSVs (Phase 2: 3 stocks; Phase 4: 10 stocks)
- `tests/unit/` GoogleTest
- `Notes/` architecture and implementation plan
