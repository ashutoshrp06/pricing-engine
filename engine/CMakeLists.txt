cmake_minimum_required(VERSION 3.15)
project(pricing_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Werror")

include_directories(include)

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
include_directories(${nlohmann_json_SOURCE_DIR})

add_library(engine_lib STATIC
    src/lp_simulator.cpp
    src/signal_generator.cpp
    src/liquidity_taker.cpp
    src/consolidated_book.cpp
    src/pricing_engine.cpp
    src/inventory.cpp
    src/metrics_publisher.cpp
)
target_include_directories(engine_lib PUBLIC include)

add_executable(engine src/main.cpp)
target_link_libraries(engine engine_lib)

add_executable(bench_throughput bench/bench_throughput.cpp)
target_link_libraries(bench_throughput engine_lib)

add_executable(bench_latency bench/bench_latency.cpp)
target_link_libraries(bench_latency engine_lib)