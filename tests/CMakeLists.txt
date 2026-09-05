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

# The snapshot comparison test needs SFML (ServerEntitySnap stores sf::Vector2f).
# It is skipped when SFML is not on the include path (e.g. minimal standalone
# configs); the main build (which fetches SFML) picks it up via this variable.
set(FLORRBT_SFML_INCLUDE_DIR "" CACHE PATH "Directory containing SFML/System/Vector2.hpp")
if(FLORRBT_SFML_INCLUDE_DIR)
    target_sources(FlorrBt_Tests PRIVATE test_snapshot_compare.cpp test_version_packet.cpp)
    target_include_directories(FlorrBt_Tests PRIVATE ${FLORRBT_SFML_INCLUDE_DIR})
endif()

enable_testing()
add_test(NAME weather_diurnal COMMAND FlorrBt_Tests)
