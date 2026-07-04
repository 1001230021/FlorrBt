cmake_minimum_required(VERSION 3.11)
project(FlorrBt)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

find_package(SFML 3 COMPONENTS Network System QUIET)
if(NOT SFML_FOUND)
    message(STATUS "SFML not found or version < 3.0. Fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG        3.0.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SFML)
endif()

file(GLOB_RECURSE SHARED_SOURCES "src/Shared/*.cpp" "src/Engine/*.cpp")
file(GLOB_RECURSE CLIENT_SOURCES "src/Client/*.cpp")
file(GLOB_RECURSE SERVER_SOURCES "src/Server/*.cpp")

add_executable(${PROJECT_NAME}_Client ${CLIENT_SOURCES} ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME}_Client PRIVATE src)
target_link_libraries(${PROJECT_NAME}_Client PRIVATE SFML::Network SFML::System)

add_executable(${PROJECT_NAME}_Server ${SERVER_SOURCES} ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME}_Server PRIVATE src)
target_link_libraries(${PROJECT_NAME}_Server PRIVATE SFML::Network SFML::System)
