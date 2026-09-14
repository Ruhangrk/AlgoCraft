# AlgoCraft

C++ NSE algorithmic trading engine. Phase 0 is the project skeleton: lock-free SPSC rings, memory pools, dedicated threads, async logging, and pluggable data-source interfaces.

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
