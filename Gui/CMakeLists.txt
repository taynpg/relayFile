cmake_minimum_required(VERSION 3.16)

project(relayFileGui LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Sql)

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
    Form/RelayTask.h Form/RelayTask.cpp Form/RelayTask.ui
    Control/LogControl.h Control/LogControl.cpp Control/LogControl.ui
    Base/MessageBoxHelper.h Base/MessageBoxHelper.cpp
    Control/OwnTableWidget.h Control/OwnTableWidget.cpp
    Base/InfoDrop.h Base/InfoDrop.cpp
    Form/WaitDialog.h Form/WaitDialog.cpp
    Form/FileMetaInfo.h Form/FileMetaInfo.cpp Form/FileMetaInfo.ui
)

set(BASE_SOURCES
    Base/WorkerThread.hpp
    Base/BaseHelper.h Base/BaseHelper.cpp
    Base/AskDirFile/BaseAskDF.h Base/AskDirFile/BaseAskDF.cpp
    Base/AskDirFile/LocalAskDF.h Base/AskDirFile/LocalAskDF.cpp
    Base/AskDirFile/RemoteAskDF.h Base/AskDirFile/RemoteAskDF.cpp
    Sqlite/ComparisonSql.h Sqlite/ComparisonSql.cpp
)

set(RESOURCE_SOURCES
    Resource/relayFileC.rc relayFile.qrc
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(relayFileGui ${PROJECT_SOURCES} ${CONTROL_SOURCES} ${BASE_SOURCES} ${RESOURCE_SOURCES}
    Form/FileMetaInfo.h Form/FileMetaInfo.cpp Form/FileMetaInfo.ui)
target_link_libraries(relayFileGui PRIVATE relayFileCore 
    Qt${QT_VERSION_MAJOR}::Widgets 
    Qt${QT_VERSION_MAJOR}::Network 
    Qt${QT_VERSION_MAJOR}::Sql
)
set_target_properties(relayFileGui PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(relayFileGui)
endif()
