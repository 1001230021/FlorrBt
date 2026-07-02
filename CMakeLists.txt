cmake_minimum_required(VERSION 3.10)
project(FlorrBt)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

file(GLOB_RECURSE SOURCES
    "src/Engine/*.cpp"
    "src/Server/*.cpp"
    "src/Shared/*.cpp"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src)

find_package(SFML 3 COMPONENTS Network System QUIET)
if(SFML_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE SFML::Network SFML::System)
endif()