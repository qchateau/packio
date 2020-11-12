cmake_minimum_required(VERSION 2.8.12)
project(UnitTests CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
set(CONAN_SYSTEM_INCLUDES ON)
conan_basic_setup()

message(STATUS "CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD}")

# Fix warning with conan
set(ignoreme ${CMAKE_EXPORT_NO_PACKAGE_REGISTRY})

set(SOURCES
    tests/main.cpp
    tests/basic.cpp
    tests/mt.cpp
    tests/incremental_buffers.cpp
)
set(CORO_SOURCES
    tests/coroutines.cpp
)

add_definitions(-DASIO_NO_DEPRECATED=1)
add_definitions(-DBOOST_ASIO_NO_DEPRECATED=1)

if (UNIX)
    add_compile_options(-Wall -Wextra -Werror)
else ()
    add_compile_options(/W4 /WX /Zc:__cplusplus /bigobj)
    # C4715 is raised in nlohmann::json
    # C4499 is raised in boost 1.74 with coroutines
    # C4702 is raised for now reason in nl_json_rpc/rpc.h
    add_compile_options(/wd4715 /wd4499 /wd4702)
    add_compile_definitions(_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING=1)
    add_compile_definitions(_WIN32_WINNT=0x0A00) # win10
endif ()

if (PACKIO_LOGGING)
    string(TOUPPER "${PACKIO_LOGGING}" LOGLEVEL)
    add_definitions(-DPACKIO_LOGGING=1)
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${LOGLEVEL})
    message(STATUS "Building with logs: ${LOGLEVEL}")
endif ()

if (PACKIO_COROUTINES)
    set(BUILD_SAMPLES ON)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcoroutines-ts)
    elseif (MSVC)
        add_compile_options(/await)
    endif ()

    message(STATUS "Building with coroutines")
    list(APPEND SOURCES ${CORO_SOURCES})
endif ()

add_executable(tests ${SOURCES})
target_link_libraries(tests ${CONAN_LIBS})

if (BUILD_SAMPLES)
    message(STATUS "Building samples")
    add_executable(basic samples/basic.cpp)
    target_link_libraries(basic ${CONAN_LIBS})
    add_executable(fibonacci samples/fibonacci.cpp)
    target_link_libraries(fibonacci ${CONAN_LIBS})
endif ()
