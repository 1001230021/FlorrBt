cmake_minimum_required(VERSION 3.11)
project(argon2 C)

set(CMAKE_C_STANDARD 99)

set(ARGON2_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/argon2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/encoding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ref.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/blake2/blake2b.c
)

add_library(argon2 STATIC ${ARGON2_SOURCES})

target_include_directories(argon2 PUBLIC include)

target_include_directories(argon2 PRIVATE src)

if(MSVC)
    target_compile_options(argon2 PRIVATE /W3)
else()
    target_compile_options(argon2 PRIVATE -Wall -Wextra)
endif()
