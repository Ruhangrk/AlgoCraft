# AlgoCraft

C++ NSE algorithmic trading engine.

**Phase 0** is the skeleton only: rings, threads, a dummy data source. No strategies and no live market.

## Build

Requires CMake 3.24+, a C++20 compiler, and network on first configure (FetchContent: GoogleTest, spdlog, Boost.Lockfree).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DALGOCRAFT_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/algocraft_engine
```

## Layout

- `include/algocraft/` public headers
- `src/` implementations (`engine`, `market_data`, `persistence`; other modules are placeholders)
- `tests/unit/` GoogleTest
- `Notes/` architecture and implementation plan
