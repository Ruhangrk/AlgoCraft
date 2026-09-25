cmake_minimum_required(VERSION 3.24)

project(AlgoCraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# GCC 15 defaults to DWARF 5; CodeLLDB often crashes on it. Force DWARF 4 for Debug.
set(CMAKE_CXX_FLAGS_DEBUG "-g -gdwarf-4" CACHE STRING "Debug C++ flags" FORCE)
set(CMAKE_C_FLAGS_DEBUG "-g -gdwarf-4" CACHE STRING "Debug C flags" FORCE)

option(ALGOCRAFT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ALGOCRAFT_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

add_library(algocraft
  src/auth/auth_service.cpp
  src/api/auth_routes.cpp
  src/api/http_helpers.cpp
  src/api/http_server.cpp
  src/api/market_routes.cpp
  src/api/workbook_routes.cpp
  src/backtest/backtest_runner.cpp
  src/backtest/backtest_service.cpp
  src/container/container_manager.cpp
  src/container/trading_container.cpp
  src/domain/symbol_table.cpp
  src/engine/phase0_runtime.cpp
  src/engine/run_manager.cpp
  src/execution/cost_calculator.cpp
  src/execution/simulated_exchange.cpp
  src/indicators/ema.cpp
  src/indicators/rsi.cpp
  src/indicators/vwap.cpp
  src/market_data/cached_provider.cpp
  src/market_data/csv_provider.cpp
  src/market_data/data_fetch_service.cpp
  src/market_data/data_source_registry.cpp
  src/market_data/dummy_provider.cpp
  src/market_data/instrument_ingest.cpp
  src/market_data/upstox_live_feed.cpp
  src/market_data/upstox_provider.cpp
  src/engine/live_run_service.cpp
  src/persistence/activity_repository.cpp
  src/persistence/async_logger.cpp
  src/persistence/backtest_repository.cpp
  src/persistence/coverage_repository.cpp
  src/persistence/instrument_repository.cpp
  src/persistence/migration_runner.cpp
  src/persistence/packed_bars.cpp
  src/persistence/rocks_bar_store.cpp
  src/persistence/sqlite_database.cpp
  src/persistence/workbook_repository.cpp
  src/portfolio/capital_manager.cpp
  src/portfolio/portfolio_ledger.cpp
  src/risk/risk_engine.cpp
  src/routing/default_router.cpp
  src/routing/routing_algo_registry.cpp
  src/strategies/bajaj_custom_strategy.cpp
  src/strategies/consecutive_up_clip.cpp
  src/strategies/ema_crossover.cpp
  src/strategies/opening_range_breakout.cpp
  src/strategies/strategy_registrations.cpp
  src/strategies/vwap_reversion.cpp
  src/workbook/workbook_manager.cpp
)

add_library(algocraft::algocraft ALIAS algocraft)

target_include_directories(algocraft
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(algocraft
  PUBLIC
    Boost::lockfree
    algocraft::sqlite3
    PkgConfig::ROCKSDB
  PRIVATE
    spdlog::spdlog
    OpenSSL::Crypto
    CURL::libcurl
    ZLIB::ZLIB
    Crow::Crow
)

algocraft_set_warnings(algocraft)

if(ALGOCRAFT_ENABLE_SANITIZERS)
  include(cmake/Sanitizers.cmake)
  algocraft_enable_sanitizers(algocraft)
endif()

add_executable(algocraft_engine src/main.cpp)
target_link_libraries(algocraft_engine PRIVATE algocraft::algocraft spdlog::spdlog)
target_compile_definitions(algocraft_engine PRIVATE
  ALGOCRAFT_DATA_DIR="${CMAKE_SOURCE_DIR}/data/1min"
  ALGOCRAFT_DB_PATH="${CMAKE_SOURCE_DIR}/data/algocraft.db"
  ALGOCRAFT_BARS_DIR="${CMAKE_SOURCE_DIR}/data/bars"
  ALGOCRAFT_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations"
)
algocraft_set_warnings(algocraft_engine)
if(ALGOCRAFT_ENABLE_SANITIZERS)
  algocraft_enable_sanitizers(algocraft_engine)
endif()

if(ALGOCRAFT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
