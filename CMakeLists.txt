cmake_minimum_required(VERSION 3.16)

project(relayFile VERSION 0.1 LANGUAGES C CXX)

set(PROJECT_URL "https://github.com/taynpg/relayFile")
option(RF_USE_GTEST "Use Test." OFF)

if(DEFINED RELEASE_MARK)
message(STATUS "Set RELEASE_MARK => ${RELEASE_MARK}.")
set(DEVELOP_MARK "release")
else()
set(DEVELOP_MARK "dev")
endif()

if (MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

list(APPEND CMAKE_PREFIX_PATH C:/Qt/6.10.3/msvc2022_64)

set(CMAKE_DEBUG_POSTFIX "d")
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/)

include(CMake/Compile.cmake)

include_directories(${PROJECT_BINARY_DIR})
set(THIRD_PATH ${CMAKE_CURRENT_LIST_DIR}/Third)
add_subdirectory(Core)
add_subdirectory(Gui)
add_subdirectory(Console)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE VERSION_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE VERSION_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
configure_file(version.h.in relayFileVersion.h)
message(STATUS "${CMAKE_SYSTEM_NAME} build dir:${PROJECT_BINARY_DIR}")
message(STATUS "VERSION_GIT_BRANCH: ${VERSION_GIT_BRANCH}")
message(STATUS "VERSION_GIT_HASH: ${VERSION_GIT_HASH}")