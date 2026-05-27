cmake_minimum_required(VERSION 3.16)
project(OrderRouter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ──────────────────────────────────────────────────────
set(WARN_FLAGS "-Wall -Wextra -Wpedantic -Wno-unused-parameter")
set(OPT_FLAGS  "-O2 -ffast-math")
set(DEBUG_FLAGS "-O0 -g -fsanitize=address,undefined")

set(CMAKE_C_FLAGS         "${WARN_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE "${OPT_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG   "${DEBUG_FLAGS}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Include directories ─────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Core library sources ────────────────────────────────────────────────
set(LIB_SOURCES
    src/or_order_book.c
    src/or_exchange.c
    src/or_routing.c
    src/or_strategy_best_price.c
    src/or_strategy_smart.c
    src/or_strategy_twap.c
    src/or_strategy_vwap.c
    src/or_router.c
    src/or_csv.c
    src/or_backtest.c
    src/or_sim_orders.c
)

# ── Shared library (for Python ctypes bridge) ───────────────────────────
add_library(orderrouter SHARED ${LIB_SOURCES})
target_link_libraries(orderrouter m)
set_target_properties(orderrouter PROPERTIES
    OUTPUT_NAME "orderrouter"
    PREFIX      "lib"
)

# ── Static library (for linking test executables) ───────────────────────
add_library(orderrouter_static STATIC ${LIB_SOURCES})
target_link_libraries(orderrouter_static m)
set_target_properties(orderrouter_static PROPERTIES OUTPUT_NAME "orderrouter")

# ── Backtest binaries ───────────────────────────────────────────────────
add_executable(or_backtest src/main_backtest.c)
target_link_libraries(or_backtest orderrouter_static m)

add_executable(or_sim_backtest src/main_sim_backtest.c)
target_link_libraries(or_sim_backtest orderrouter_static m)

# ── Test executables ────────────────────────────────────────────────────
enable_testing()

add_executable(test_order_book tests/test_order_book.c)
target_link_libraries(test_order_book orderrouter_static m)
add_test(NAME OrderBook COMMAND test_order_book)

add_executable(test_exchange tests/test_exchange.c)
target_link_libraries(test_exchange orderrouter_static m)
add_test(NAME Exchange COMMAND test_exchange)

add_executable(test_strategies tests/test_strategies.c)
target_link_libraries(test_strategies orderrouter_static m)
add_test(NAME Strategies COMMAND test_strategies)

add_executable(test_latency tests/test_latency.c)
target_link_libraries(test_latency orderrouter_static m)
add_test(NAME Latency COMMAND test_latency)

add_executable(test_sim_orders tests/test_sim_orders.c)
target_link_libraries(test_sim_orders orderrouter_static m)
add_test(NAME SimOrders COMMAND test_sim_orders)

# ── Install ─────────────────────────────────────────────────────────────
install(TARGETS orderrouter or_backtest or_sim_backtest
        LIBRARY  DESTINATION lib
        RUNTIME  DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include/order_router)

# ── Summary ─────────────────────────────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C standard : C${CMAKE_C_STANDARD}")
message(STATUS "C flags    : ${CMAKE_C_FLAGS}")
