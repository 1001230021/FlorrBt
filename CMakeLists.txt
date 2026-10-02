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

        # The Windows MySQL ZIP ships libmysql.dll separately from its
        # OpenSSL runtime DLLs.  Linking succeeds without these files, but
        # the server cannot start because libmysql.dll loads both SSL and
        # crypto at process startup.  Copy the complete runtime set next to
        # the executable whenever the files are available.
        if(WIN32)
            set(FLORRBT_MYSQL_RUNTIME_DIR "" CACHE PATH
                "Directory containing the MySQL/MariaDB runtime DLLs")
            get_filename_component(_florrbt_mysql_lib_dir
                "${FLORRBT_MYSQL_LIBRARY}" DIRECTORY)
            get_filename_component(_florrbt_mysql_root_dir
                "${_florrbt_mysql_lib_dir}" DIRECTORY)

            set(_florrbt_mysql_runtime_search_paths
                "${FLORRBT_MYSQL_RUNTIME_DIR}"
                "${_florrbt_mysql_lib_dir}"
                "${_florrbt_mysql_root_dir}/bin"
                "${_florrbt_mysql_root_dir}"
            )
            list(FILTER _florrbt_mysql_runtime_search_paths EXCLUDE REGEX "^$")

            find_file(_florrbt_mysql_runtime_dll
                NAMES libmysql.dll libmariadb.dll mariadb.dll
                PATHS ${_florrbt_mysql_runtime_search_paths}
                NO_DEFAULT_PATH)
            find_file(_florrbt_mysql_ssl_dll
                NAMES libssl-3-x64.dll libssl-3.dll libssl-1_1-x64.dll libssl.dll
                PATHS ${_florrbt_mysql_runtime_search_paths}
                NO_DEFAULT_PATH)
            find_file(_florrbt_mysql_crypto_dll
                NAMES libcrypto-3-x64.dll libcrypto-3.dll libcrypto-1_1-x64.dll libcrypto.dll
                PATHS ${_florrbt_mysql_runtime_search_paths}
                NO_DEFAULT_PATH)

            foreach(_florrbt_mysql_runtime_file IN ITEMS
                    "${_florrbt_mysql_runtime_dll}"
                    "${_florrbt_mysql_ssl_dll}"
                    "${_florrbt_mysql_crypto_dll}")
                if(_florrbt_mysql_runtime_file AND EXISTS "${_florrbt_mysql_runtime_file}")
                    add_custom_command(TARGET ${PROJECT_NAME}_Server POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_florrbt_mysql_runtime_file}"
                            "$<TARGET_FILE_DIR:${PROJECT_NAME}_Server>"
                        VERBATIM)
                else()
                    message(WARNING
                        "MySQL runtime DLL was not found; the Windows server may not start. "
                        "Set FLORRBT_MYSQL_RUNTIME_DIR to the directory containing "
                        "libmysql.dll, libssl*.dll and libcrypto*.dll.")
                endif()
            endforeach()
        endif()
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
