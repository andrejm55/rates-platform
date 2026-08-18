cmake_minimum_required(VERSION 3.20)
project(cpp_rates_derivatives_engine VERSION 0.1.0 LANGUAGES CXX)

option(RATES_BUILD_TESTS "Build the test executable" ON)
option(RATES_BUILD_PYTHON "Build optional pybind11 bindings" OFF)
option(RATES_ENABLE_SANITIZERS "Enable address and undefined behaviour sanitizers" OFF)
option(RATES_BUILD_BENCHMARKS "Build the dependency-free benchmark executable" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rates_core
    src/calibration/curve_bootstrap.cpp
    src/core/date.cpp
    src/core/math.cpp
    src/market/yield_curve.cpp
    src/models/option_models.cpp
    src/pricing/pricers.cpp
    src/risk/risk_engine.cpp
    src/portfolio/portfolio_pricer.cpp
    src/persistence/json.cpp
    src/services/application_service.cpp
)

set_target_properties(rates_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(rates_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(rates_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(rates_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rates_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(RATES_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(rates_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(rates_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(rates_cli apps/cli/main.cpp)
target_link_libraries(rates_cli PRIVATE rates_core)

if(RATES_BUILD_TESTS)
    enable_testing()
    add_executable(rates_tests tests/test_main.cpp)
    target_link_libraries(rates_tests PRIVATE rates_core)
    add_test(NAME rates_tests COMMAND rates_tests)
endif()

if(RATES_BUILD_BENCHMARKS)
    add_executable(rates_benchmark benchmarks/basic_benchmark.cpp)
    target_link_libraries(rates_benchmark PRIVATE rates_core)
endif()

if(RATES_BUILD_PYTHON)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(rates_engine_py bindings/python/module.cpp)
    target_link_libraries(rates_engine_py PRIVATE rates_core)
endif()

install(TARGETS rates_core rates_cli)
