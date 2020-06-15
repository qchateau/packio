cmake_minimum_required(VERSION 2.8.12)
project(UnitTests CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

message(STATUS "CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD}")

# Fix warning with conan
set(ignoreme ${CMAKE_EXPORT_NO_PACKAGE_REGISTRY})

set(SOURCES
    tests/main.cpp
    tests/basic.cpp
    tests/mt.cpp
)
set(CORO_SOURCES
    tests/coroutines.cpp
)

add_definitions(-DASIO_NO_DEPRECATED=1)
add_definitions(-DBOOST_ASIO_NO_DEPRECATED=1)

if (UNIX)
    # no-unused-parameter because boost has warnings with -fno-rtti
    add_compile_options(-Wall -Wextra -Werror -fno-rtti -Wno-unused-parameter)
    add_definitions(-DASIO_NO_TYPEID=1)
else ()
    add_compile_options(/W4 /WX /Zc:__cplusplus /bigobj /GR-)
    add_definitions(/D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING=1)
endif ()

if (PACKIO_LOGGING)
    string(TOUPPER "${PACKIO_LOGGING}" LOGLEVEL)
    add_definitions(-DPACKIO_LOGGING=1)
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${LOGLEVEL})
    message(STATUS "Building with logs: ${LOGLEVEL}")
endif ()

if (PACKIO_COROUTINES)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcoroutines-ts)
    elseif (MSVC)
        add_compile_options(/await)
    endif ()

    message(STATUS "Building with coroutines")
    list(APPEND SOURCES ${CORO_SOURCES})

    message(STATUS "Building samples")
    set(SAMPLES ON)
endif ()

add_executable(tests ${SOURCES})
target_link_libraries(tests ${CONAN_LIBS})

if (SAMPLES)
    add_executable(basic samples/basic.cpp)
    target_link_libraries(basic ${CONAN_LIBS})
    add_executable(fibonacci samples/fibonacci.cpp)
    target_link_libraries(fibonacci ${CONAN_LIBS})
endif ()
