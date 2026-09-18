include(FetchContent)

set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# --- GoogleTest ---
if(ALGOCRAFT_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

# --- spdlog ---
FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# --- Boost.Lockfree (modular CMake tarball; not the full superproject) ---
set(BOOST_INCLUDE_LIBRARIES lockfree)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_SKIP_INSTALL_RULES ON)

FetchContent_Declare(
  Boost
  URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Boost)

# --- SQLite amalgamation (persistence thread only; not linked into Thread 0) ---
FetchContent_Declare(
  sqlite_amalgamation
  URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite_amalgamation)

add_library(algocraft_sqlite3 STATIC "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
add_library(algocraft::sqlite3 ALIAS algocraft_sqlite3)
target_include_directories(algocraft_sqlite3 PUBLIC "${sqlite_amalgamation_SOURCE_DIR}")
target_compile_definitions(algocraft_sqlite3
  PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DQS=0
)
set_target_properties(algocraft_sqlite3 PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
  target_compile_options(algocraft_sqlite3 PRIVATE /w)
else()
  target_compile_options(algocraft_sqlite3 PRIVATE -w)
endif()
find_package(Threads REQUIRED)
target_link_libraries(algocraft_sqlite3 PUBLIC Threads::Threads)

# --- RocksDB (system package: librocksdb-dev) ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(ROCKSDB REQUIRED IMPORTED_TARGET rocksdb)

# --- OpenSSL (auth JWT / PBKDF2) ---
find_package(OpenSSL REQUIRED)

# --- libcurl (Upstox REST; Thread 4 / persistence path only) ---
find_package(CURL REQUIRED)
