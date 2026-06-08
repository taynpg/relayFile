cmake_minimum_required(VERSION 3.16)

project(relayFileServer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

if (WIN32)
    MESSAGE(STATUS "WINDOWS ENV.")
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DSPDLOG_UTF8_TO_WCHAR_CONSOLE)
    add_definitions(-DSPDLOG_WCHAR_TO_UTF8_SUPPORT)
endif()

add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
add_definitions(-DFMT_HEADER_ONLY)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

add_executable(relayFileServer Server.cpp)
add_executable(relayFileClient Client.cpp)

target_include_directories(relayFileServer PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(relayFileServer PUBLIC relayFileCore Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

target_include_directories(relayFileClient PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(relayFileClient PUBLIC relayFileCore Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)

if (COMPILE_NAME MATCHES "MINGW")
    target_link_libraries(relayFileServer PUBLIC ws2_32 mswsock)
endif()