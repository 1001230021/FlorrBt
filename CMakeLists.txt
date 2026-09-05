cmake_minimum_required(VERSION 3.11)
project(FlorrBt VERSION 1.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

find_package(SFML 3 COMPONENTS Graphics Network System QUIET)
if(NOT SFML_FOUND)
    message(STATUS "SFML not found or version < 3.0. Fetching from GitHub...")
    include(FetchContent)
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG        3.1.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SFML)
endif()

add_subdirectory(externals/libtmj)
add_subdirectory(externals/argon2)

file(GLOB_RECURSE SHARED_SOURCES "src/Shared/*.cpp" "src/Engine/*.cpp")
file(GLOB_RECURSE SERVER_SOURCES "src/Server/*.cpp")

add_executable(${PROJECT_NAME}_Server ${SERVER_SOURCES} ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME}_Server PRIVATE src)
target_link_libraries(${PROJECT_NAME}_Server PRIVATE SFML::Graphics SFML::Network SFML::System tmj argon2)

# Optional MySQL/MariaDB client for the account persistence backend
# (account_backend mysql in server.cfg). Works with libmysql (MySQL zip) and
# MariaDB Connector/C; without either the server builds with the JSON backend
# only and account_backend=mysql fails at startup with a clear message.
option(FLORRBT_WITH_MYSQL "Build the MySQL account persistence backend" ON)
if(FLORRBT_WITH_MYSQL)
    find_path(FLORRBT_MYSQL_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql mariadb)
    find_library(FLORRBT_MYSQL_LIBRARY NAMES libmysql mysqlclient mariadb libmariadb mariadbclient)
    if(FLORRBT_MYSQL_INCLUDE_DIR AND FLORRBT_MYSQL_LIBRARY)
        target_compile_definitions(${PROJECT_NAME}_Server PRIVATE FLORRBT_WITH_MYSQL)
        target_include_directories(${PROJECT_NAME}_Server PRIVATE ${FLORRBT_MYSQL_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME}_Server PRIVATE ${FLORRBT_MYSQL_LIBRARY})
        message(STATUS "MySQL account backend: enabled (${FLORRBT_MYSQL_LIBRARY})")
    else()
        message(STATUS "MySQL client library not found - building without the MySQL account backend")
    endif()
endif()

add_executable(${PROJECT_NAME}_Launcher src/Launcher/main.cpp src/Engine/json_value.cpp)
target_include_directories(${PROJECT_NAME}_Launcher PRIVATE src)
if(WIN32)
    target_compile_definitions(${PROJECT_NAME}_Server PRIVATE NOMINMAX)
endif()

add_subdirectory(tests)
