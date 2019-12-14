cmake_minimum_required(VERSION 2.8.12)
project(UnitTests CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_compile_options(-Wall -Wextra -Werror)

add_definitions(-DBOOST_ASIO_NO_DEPRECATED=1)
if (WITH_LOGS)
    add_definitions(-DPACKIO_LOGGING=1)
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_TRACE)
    message(STATUS "Building with logs")
endif ()
set(CMAKE_CXX_STANDARD 17)

add_executable(basic basic.cpp)
target_link_libraries(basic ${CONAN_LIBS})
add_executable(mt mt.cpp)
target_link_libraries(mt ${CONAN_LIBS})
