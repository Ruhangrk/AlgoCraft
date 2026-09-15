include(CTest)

add_executable(algocraft_tests
  unit/test_spsc_ring.cpp
  unit/test_memory_pool.cpp
  unit/test_domain.cpp
  unit/test_symbol_table.cpp
  unit/test_events.cpp
  unit/test_data_source_registry.cpp
  unit/test_async_logger.cpp
  unit/test_phase0_runtime.cpp
)

target_link_libraries(algocraft_tests
  PRIVATE
    algocraft::algocraft
    GTest::gtest_main
)

algocraft_set_warnings(algocraft_tests)

if(ALGOCRAFT_ENABLE_SANITIZERS)
  algocraft_enable_sanitizers(algocraft_tests)
endif()

add_test(NAME algocraft_unit COMMAND algocraft_tests)
set_tests_properties(algocraft_unit PROPERTIES
  ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
)
