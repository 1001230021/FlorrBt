# Standalone test project: depends only on the standard library, so it can be
# configured without SFML/jansson. The top-level CMakeLists.txt adds this via
# add_subdirectory(tests); it can also be configured directly:
#   cmake -S tests -B build-tests && cmake --build build-tests
cmake_minimum_required(VERSION 3.11)
project(FlorrBt_Tests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

add_executable(FlorrBt_Tests
    test_framework.h
    test_main.cpp
    test_weather_diurnal.cpp
    test_utf8_truncate.cpp
    ../src/Server/Game/systems/weather_diurnal_system.cpp
)
target_include_directories(FlorrBt_Tests PRIVATE .. ../src)

enable_testing()
add_test(NAME weather_diurnal COMMAND FlorrBt_Tests)
