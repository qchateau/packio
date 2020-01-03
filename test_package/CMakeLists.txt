cmake_minimum_required(VERSION 2.8.12)
project(UnitTests CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

if (UNIX)
    add_compile_options(-Wall -Wextra -Werror)
else ()
    add_compile_options(/W4 /WX /Zc:__cplusplus /bigobj)
    add_definitions(/D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING=1)
endif ()

add_definitions(-DBOOST_ASIO_NO_DEPRECATED=1)
if (PACKIO_LOGGING)
    string(TOUPPER "${PACKIO_LOGGING}" LOGLEVEL)
    add_definitions(-DPACKIO_LOGGING=1)
    add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${LOGLEVEL})
    message(STATUS "Building with logs: ${LOGLEVEL}")
endif ()
set(CMAKE_CXX_STANDARD 17)

add_executable(basic basic.cpp)
target_link_libraries(basic ${CONAN_LIBS})
add_executable(mt mt.cpp)
target_link_libraries(mt ${CONAN_LIBS})
