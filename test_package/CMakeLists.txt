cmake_minimum_required(VERSION 3.16)
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
    tests/basic_test_server_crash.cpp
    tests/basic_test_typical_usage.cpp
    tests/basic_test_args_types.cpp
    tests/basic_test_timeout.cpp
    tests/basic_test_functions.cpp
    tests/basic_test_named_arguments.cpp
    tests/basic_test_default_arguments.cpp
    tests/basic_test_dispatcher.cpp
    tests/basic_test_end_of_work.cpp
    tests/basic_test_special_callables.cpp
    tests/basic_test_response_after_disconnect.cpp
    tests/basic_test_shared_dispatcher.cpp
    tests/basic_test_errors.cpp
    tests/basic_test_coroutine.cpp
    tests/mt_test_big_msg.cpp
    tests/mt_test_many_func.cpp
    tests/mt_test_same_func.cpp
    tests/incremental_buffers.cpp
)

add_compile_definitions(ASIO_NO_DEPRECATED=1)
add_compile_definitions(BOOST_ASIO_NO_DEPRECATED=1)

if (UNIX)
    add_compile_options(-Wall -Wextra -Werror -fdiagnostics-color)
else ()
    add_compile_options(/W4 /WX /Zc:__cplusplus /bigobj)
    # C4715 is raised in nlohmann::json
    add_compile_options(/wd4715)
    # C4702 is raised for no reason in nl_json_rpc/rpc.h
    add_compile_options(/wd4702)
    add_compile_definitions(_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING=1)
    add_compile_definitions(_WIN32_WINNT=0x0A00) # win10
endif ()

if (PACKIO_LOGGING)
    string(TOUPPER "${PACKIO_LOGGING}" LOGLEVEL)
    add_compile_definitions(PACKIO_LOGGING=1)
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${LOGLEVEL})
    message(STATUS "Building with logs: ${LOGLEVEL}")
endif ()

add_executable(tests ${SOURCES})
target_link_libraries(tests ${CONAN_LIBS})

add_executable(perf samples/perf.cpp)
target_link_libraries(perf ${CONAN_LIBS})

if (BUILD_SAMPLES)
    message(STATUS "Building samples")

    add_executable(basic samples/basic.cpp)
    target_link_libraries(basic ${CONAN_LIBS})

    add_executable(ssl_stream samples/ssl.cpp)
    target_link_libraries(ssl_stream ${CONAN_LIBS})

    add_executable(fibonacci samples/fibonacci.cpp)
    target_link_libraries(fibonacci ${CONAN_LIBS})
endif ()
