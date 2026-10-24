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
    test_circle_contact.cpp
    test_petal_spawn_rules.cpp
    test_content_manifests.cpp
    test_server_safety.cpp
    test_combat_feedback.cpp
    test_skill_result.cpp
    test_mob_radius.cpp
    test_dev_debug.cpp
    ../src/Server/Game/extensions/weather_extension.cpp
)
target_include_directories(FlorrBt_Tests PRIVATE .. ../src)

# The wire protocol uses the dependency-free shared math types, so these tests
# are part of the standalone test target and do not need an SFML include path.
target_sources(FlorrBt_Tests PRIVATE test_snapshot_compare.cpp test_version_packet.cpp)

enable_testing()
add_test(NAME weather_diurnal COMMAND FlorrBt_Tests)
