cmake_minimum_required(VERSION 3.16)

project(relayFile LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
    main.cpp
    relayFile.cpp
    relayFile.h
    relayFile.ui
)

set(CONTROL_SOURCES
    Control/ComparisonControl.h Control/ComparisonControl.cpp Control/ComparisonControl.ui
    Control/ConnectorControl.h Control/ConnectorControl.cpp Control/ConnectorControl.ui
    Control/ExplorerControl.h Control/ExplorerControl.cpp Control/ExplorerControl.ui
    Control/RelayTask.h Control/RelayTask.cpp Control/RelayTask.ui
    Control/LogControl.h Control/LogControl.cpp Control/LogControl.ui
)

add_executable(relayFile ${PROJECT_SOURCES} ${CONTROL_SOURCES})
target_link_libraries(relayFile PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
set_target_properties(relayFile PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(relayFile)
endif()
