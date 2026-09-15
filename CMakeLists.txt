cmake_minimum_required(VERSION 3.24)

project(AlgoCraft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ALGOCRAFT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ALGOCRAFT_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

add_library(algocraft
  src/backtest/backtest_runner.cpp
  src/domain/symbol_table.cpp
  src/engine/phase0_runtime.cpp
  src/execution/cost_calculator.cpp
  src/execution/simulated_exchange.cpp
  src/indicators/ema.cpp
  src/indicators/rsi.cpp
  src/indicators/vwap.cpp
  src/market_data/csv_provider.cpp
  src/market_data/data_source_registry.cpp
  src/market_data/dummy_provider.cpp
  src/persistence/async_logger.cpp
  src/strategies/consecutive_up_clip.cpp
  src/strategies/ema_crossover.cpp
  src/strategies/opening_range_breakout.cpp
  src/strategies/strategy_registrations.cpp
  src/strategies/vwap_reversion.cpp
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
  PRIVATE
    spdlog::spdlog
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
)
algocraft_set_warnings(algocraft_engine)
if(ALGOCRAFT_ENABLE_SANITIZERS)
  algocraft_enable_sanitizers(algocraft_engine)
endif()

if(ALGOCRAFT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
