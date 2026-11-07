cmake_minimum_required(VERSION 3.24)
# 启用语言前固定动态 CRT，避免 libtmj 的自定义编译参数回落到不兼容的 /MT。
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
    # 旧子项目重设最低版本时也须沿用同一 CRT 策略。
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()
project(FlorrBt VERSION 1.0.9)
include(cmake/MsvcIncludePrefix.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    add_compile_options(/utf-8)
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

# 独立配置传输依赖，避免 curl 的选项污染其他库。
add_subdirectory(cmake/curl)

file(GLOB_RECURSE SHARED_SOURCES CONFIGURE_DEPENDS "src/Shared/*.cpp" "src/Engine/*.cpp")
file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS "src/Server/*.cpp")

list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Server/main.cpp")
list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Server/commands.cpp")
add_library(${PROJECT_NAME}_ServerCore STATIC ${SERVER_SOURCES} ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME}_ServerCore PUBLIC src)
target_link_libraries(${PROJECT_NAME}_ServerCore PUBLIC SFML::Graphics SFML::Network SFML::System tmj argon2 CURL::libcurl)
add_executable(${PROJECT_NAME}_Server src/Server/main.cpp)
target_link_libraries(${PROJECT_NAME}_Server PRIVATE ${PROJECT_NAME}_ServerCore)
# 静态命令注册没有供外部引用的符号，必须直接链接，不能依赖静态库按需抽取。
target_sources(${PROJECT_NAME}_Server PRIVATE src/Server/commands.cpp)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME}_Server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:CURL::libcurl> $<TARGET_FILE_DIR:${PROJECT_NAME}_Server>)
endif()

# Optional MySQL/MariaDB client for the account persistence backend
# (account_backend mysql in server.cfg). Works with libmysql (MySQL zip) and
# MariaDB Connector/C; without either the server builds with the JSON backend
# only and account_backend=mysql fails at startup with a clear message.
option(FLORRBT_WITH_MYSQL "Build the MySQL account persistence backend" ON)
if(FLORRBT_WITH_MYSQL)
    find_path(FLORRBT_MYSQL_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql mariadb)
    find_library(FLORRBT_MYSQL_LIBRARY NAMES libmysql mysqlclient mariadb libmariadb mariadbclient)
    if(FLORRBT_MYSQL_INCLUDE_DIR AND FLORRBT_MYSQL_LIBRARY)
        target_compile_definitions(${PROJECT_NAME}_ServerCore PRIVATE FLORRBT_WITH_MYSQL)
        target_include_directories(${PROJECT_NAME}_ServerCore PRIVATE ${FLORRBT_MYSQL_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME}_ServerCore PUBLIC ${FLORRBT_MYSQL_LIBRARY})
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
    target_compile_definitions(${PROJECT_NAME}_ServerCore PUBLIC NOMINMAX)
endif()

add_subdirectory(tests)

option(FLORRBT_BUILD_RUNTIME_TESTS "Build headless server lifecycle/snapshot regressions" OFF)
if(FLORRBT_BUILD_RUNTIME_TESTS)
    # Persistence regressions need neither a running server nor the world/
    # renderer libraries. Keep their disk fixtures isolated under artifacts.
    add_executable(FlorrBt_AccountTests tests/test_main.cpp tests/test_account_transactions.cpp
        src/Server/Persistence/account_store.cpp src/Server/Persistence/mysql_account_backend.cpp
        src/Server/Persistence/economy_codec.cpp src/Server/Persistence/account_identity_codec.cpp src/Engine/json_value.cpp)
    target_include_directories(FlorrBt_AccountTests PRIVATE src tests)
    target_link_libraries(FlorrBt_AccountTests PRIVATE argon2)
    if(WIN32)
        target_compile_definitions(FlorrBt_AccountTests PRIVATE NOMINMAX)
    endif()
    enable_testing()
    add_test(NAME account_transactions COMMAND FlorrBt_AccountTests)
    set_tests_properties(account_transactions PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(FlorrBt_RuntimeTests tests/test_main.cpp tests/test_runtime_repairs.cpp)
    target_include_directories(FlorrBt_RuntimeTests PRIVATE src tests)
    target_link_libraries(FlorrBt_RuntimeTests PRIVATE ${PROJECT_NAME}_ServerCore)
    target_sources(FlorrBt_RuntimeTests PRIVATE src/Server/commands.cpp)
    if(WIN32)
        target_compile_definitions(FlorrBt_RuntimeTests PRIVATE NOMINMAX)
    endif()
    enable_testing()
    add_test(NAME runtime_repairs COMMAND FlorrBt_RuntimeTests)
    set_tests_properties(runtime_repairs PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    if(WIN32)
        set_tests_properties(runtime_repairs PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:SFML::Graphics>;PATH=path_list_prepend:$<TARGET_FILE_DIR:tmj>;PATH=path_list_prepend:$<TARGET_FILE_DIR:CURL::libcurl>")
    endif()
    add_executable(FlorrBt_TransportTests tests/test_main.cpp tests/test_remote_transport.cpp
        src/Server/Transport/remote_transport.cpp src/Server/Transport/report_review.cpp
        src/Server/Auth/email_verification_service.cpp src/Engine/json_value.cpp)
    target_include_directories(FlorrBt_TransportTests PRIVATE src tests)
    target_link_libraries(FlorrBt_TransportTests PRIVATE CURL::libcurl SFML::Network SFML::System)
    add_test(NAME remote_transport COMMAND FlorrBt_TransportTests)
    set_tests_properties(remote_transport PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    if(WIN32)
        target_compile_definitions(FlorrBt_TransportTests PRIVATE NOMINMAX)
        set_tests_properties(remote_transport PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:SFML::System>;PATH=path_list_prepend:$<TARGET_FILE_DIR:CURL::libcurl>")
    endif()
endif()
