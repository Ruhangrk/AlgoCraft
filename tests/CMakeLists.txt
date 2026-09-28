include(CTest)

add_executable(algocraft_tests
  unit/test_spsc_ring.cpp
  unit/test_domain.cpp
  unit/test_symbol_table.cpp
  unit/test_events.cpp
  unit/test_data_source_registry.cpp
  unit/test_log_hub.cpp
  unit/test_persistence_service.cpp
  unit/test_strategy_catalog.cpp
  unit/test_sqlite.cpp
  unit/test_bar_store.cpp
  unit/test_data_fetch.cpp
  unit/test_indicators.cpp
  unit/test_execution.cpp
  unit/test_ring_execution.cpp
  unit/test_live_control.cpp
  unit/test_backtest.cpp
  unit/test_backtest_service.cpp
  unit/test_consecutive_up_clip.cpp
  unit/test_two_consecutive_bars.cpp
  unit/test_dip5_mean_revert.cpp
  unit/test_reliance_prev5_avg_break.cpp
  unit/test_workbook.cpp
  unit/test_workbook_repo.cpp
  unit/test_portfolio.cpp
  unit/test_risk.cpp
  unit/test_container.cpp
  unit/test_scheduler.cpp
  unit/test_run.cpp
  unit/test_activity_repo.cpp
  unit/test_auth.cpp
  unit/test_instruments.cpp
  unit/test_upstox.cpp
  unit/test_live_feed.cpp
  unit/test_status_sse_hub.cpp
)

target_link_libraries(algocraft_tests
  PRIVATE
    algocraft::algocraft
    algocraft::sqlite3
    GTest::gtest_main
)

target_compile_definitions(algocraft_tests
  PRIVATE
    ALGOCRAFT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ALGOCRAFT_DATA_DIR="${CMAKE_SOURCE_DIR}/data/1min"
    ALGOCRAFT_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations"
    ALGOCRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

algocraft_set_warnings(algocraft_tests)

if(ALGOCRAFT_ENABLE_SANITIZERS)
  algocraft_enable_sanitizers(algocraft_tests)
endif()

add_test(NAME algocraft_unit COMMAND algocraft_tests)
set_tests_properties(algocraft_unit PROPERTIES
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
)
