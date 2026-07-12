cmake_minimum_required(VERSION 3.11)
project(FlorrBt)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies as static libraries" FORCE)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

option(FLORRBT_USE_SYSTEM_SFML "Use an installed SFML package" OFF)

if(FLORRBT_USE_SYSTEM_SFML)
    find_package(SFML 3 REQUIRED COMPONENTS Graphics Network System)
else()
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)

    # SFML uses FetchContent for these dependencies. Point each declaration at
    # the vendored source so configuration is deterministic and works offline.
    set(FETCHCONTENT_SOURCE_DIR_MBEDTLS "${CMAKE_CURRENT_SOURCE_DIR}/externals/mbedtls" CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_LIBSSH2 "${CMAKE_CURRENT_SOURCE_DIR}/externals/libssh2" CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_FREETYPE "${CMAKE_CURRENT_SOURCE_DIR}/externals/freetype" CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_HARFBUZZ "${CMAKE_CURRENT_SOURCE_DIR}/externals/harfbuzz" CACHE PATH "" FORCE)
    set(FETCHCONTENT_SOURCE_DIR_SHEENBIDI "${CMAKE_CURRENT_SOURCE_DIR}/externals/sheenbidi" CACHE PATH "" FORCE)

    add_subdirectory(externals/sfml)

    # SFML's dependency integration assumes FetchContent's default *-src
    # layout. Source overrides use our vendored paths instead, so supply the
    # public headers explicitly and avoid FreeType's path-sensitive unity
    # exclusions.
    foreach(MBEDTLS_TARGET mbedtls mbedcrypto mbedx509)
        target_include_directories(${MBEDTLS_TARGET} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/externals/mbedtls/include>)
    endforeach()
    target_include_directories(libssh2_static PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/externals/mbedtls/include")
    set_target_properties(freetype PROPERTIES UNITY_BUILD OFF)

    if(MSVC)
        foreach(THIRD_PARTY_TARGET freetype harfbuzz SheenBidi)
            get_target_property(THIRD_PARTY_OPTIONS ${THIRD_PARTY_TARGET} COMPILE_OPTIONS)
            if(THIRD_PARTY_OPTIONS)
                list(FILTER THIRD_PARTY_OPTIONS EXCLUDE REGEX "^-w$")
                set_target_properties(${THIRD_PARTY_TARGET} PROPERTIES COMPILE_OPTIONS "${THIRD_PARTY_OPTIONS}")
            endif()
            target_compile_options(${THIRD_PARTY_TARGET} PRIVATE /W0)
        endforeach()
        target_compile_options(libssh2_static PRIVATE /wd4701)
    endif()
endif()

set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
set(JANSSON_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/jansson)
add_library(jansson::jansson ALIAS jansson)
target_include_directories(jansson PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/externals/jansson/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/externals/jansson/include>)
add_subdirectory(externals/libtmj)
add_subdirectory(externals/argon2)

if(MSVC)
    target_compile_definitions(argon2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(jansson PRIVATE /wd4334)
endif()

file(GLOB_RECURSE SHARED_SOURCES "src/Shared/*.cpp" "src/Engine/*.cpp")
file(GLOB_RECURSE SERVER_SOURCES "src/Server/*.cpp")

add_executable(${PROJECT_NAME}_Server ${SERVER_SOURCES} ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME}_Server PRIVATE src)
target_compile_definitions(${PROJECT_NAME}_Server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(${PROJECT_NAME}_Server PRIVATE /wd4099)
endif()
target_link_libraries(${PROJECT_NAME}_Server PRIVATE SFML::Graphics SFML::Network SFML::System tmj argon2)
