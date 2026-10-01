cmake_minimum_required(VERSION 3.16)

project(relayFileServerAlone VERSION 0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DSPDLOG_UTF8_TO_WCHAR_CONSOLE)
    add_definitions(-DSPDLOG_WCHAR_TO_UTF8_SUPPORT)
endif()

add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
add_definitions(-DASIO_STANDALONE)

if(DEFINED RELEASE_MARK)
    set(DEVELOP_MARK "release")
else()
    set(DEVELOP_MARK "dev")
endif()

# 版本信息（复用主项目的 version.h.in）
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE VERSION_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE VERSION_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
configure_file(${CMAKE_CURRENT_LIST_DIR}/../version.h.in ${CMAKE_BINARY_DIR}/relayFileVersion.h)

set(RELAY_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# 复用主项目中无 Qt 依赖的协议层源码
add_executable(relayFileServerAlone
    main.cpp
    AsioServer.h
    AsioServer.cpp
    ${RELAY_ROOT}/Core/Protocol/Protocol.cpp
    ${RELAY_ROOT}/Core/Protocol/Message.cpp
    ${RELAY_ROOT}/Core/Protocol/FileMeta.cpp
    ${RELAY_ROOT}/Core/Utils/miniUtil.cpp
)

target_include_directories(relayFileServerAlone PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_BINARY_DIR}
    ${RELAY_ROOT}/Core
    ${RELAY_ROOT}/Third
)

if(WIN32)
    target_link_libraries(relayFileServerAlone PRIVATE ws2_32 mswsock dbghelp)
endif()
