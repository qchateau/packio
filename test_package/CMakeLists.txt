cmake_minimum_required(VERSION 2.8.12)
project(UnitTests CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_CXX_STANDARD 17)
add_definitions(-DBOOST_ASIO_NO_DEPRECATED=1)

if (UNIX)
    add_compile_options(-Wall -Wextra -Werror)
else ()
    add_compile_options(/W4 /WX /Zc:__cplusplus /bigobj)
    add_definitions(/D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING=1)
endif ()

if (PACKIO_NO_RTTI)
    message(STATUS "Disabling RTTI")
    if (UNIX)
        add_compile_options(-fno-rtti)
        # no-unused-parameter because boost has warnings with -fno-rtti
        add_compile_options(-Wno-unused-parameter)
    else ()
        add_compile_options(/GR-)
    endif ()
endif ()

if (PACKIO_LOGGING)
    string(TOUPPER "${PACKIO_LOGGING}" LOGLEVEL)
    add_definitions(-DPACKIO_LOGGING=1)
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${LOGLEVEL})
    message(STATUS "Building with logs: ${LOGLEVEL}")
endif ()


if (PACKIO_COROUTINES)
    message(STATUS "Building with coroutines")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcoroutines-ts)
    elseif (MSVC)
        add_compile_options(/await)
    else ()
        message(FATAL_ERROR "Compiler '${CMAKE_CXX_COMPILER_ID}' not supported for coroutines")
    endif ()
endif ()

add_executable(basic basic.cpp)
target_link_libraries(basic ${CONAN_LIBS})
add_executable(mt mt.cpp)
target_link_libraries(mt ${CONAN_LIBS})
