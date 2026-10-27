cmake_minimum_required(VERSION 3.16)
project(qnetstats)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT WIN32)
    message(FATAL_ERROR "Building QNetStats is not supported on ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(USE_QT "AUTO" CACHE STRING "Qt major version: AUTO, 5, or 6")
set_property(CACHE USE_QT PROPERTY STRINGS AUTO 5 6)

if(USE_QT STREQUAL "AUTO")
    find_package(Qt6 6.0 QUIET CONFIG COMPONENTS Core Network Widgets)
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
    else()
        find_package(Qt5 5.11 REQUIRED CONFIG COMPONENTS Core Network Widgets)
        set(QT_VERSION_MAJOR 5)
    endif()
elseif(USE_QT STREQUAL "5")
    find_package(Qt5 5.11 REQUIRED CONFIG COMPONENTS Core Network Widgets)
    set(QT_VERSION_MAJOR 5)
elseif(USE_QT STREQUAL "6")
    find_package(Qt6 6.0 REQUIRED CONFIG COMPONENTS Core Network Widgets)
    set(QT_VERSION_MAJOR 6)
else()
    message(FATAL_ERROR "USE_QT must be AUTO, 5, or 6")
endif()

message(STATUS "Building QNetStats with Qt ${QT_VERSION_MAJOR}")

set(CMAKE_AUTOMOC on)
set(CMAKE_AUTOUIC on)
set(CMAKE_AUTORCC on)

add_executable(qnetstats
        src/interfacehistory.cpp
        src/interfacehistory.h
        src/textstatistics.cpp
        src/chart.cpp
        src/colorbutton.cpp
        src/colorbutton.h
        src/configure.cpp
        src/qnetstats.cpp
        src/qnetstatsview.cpp
        src/main.cpp
        src/statistics.cpp
        src/configurebase.ui
        src/statisticsbase.ui
        res/qnetstats.qrc
)

target_include_directories(qnetstats PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(qnetstats PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Widgets
)

if(WIN32)
    set_target_properties(qnetstats PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(qnetstats PRIVATE
        WINVER=0x0A00 _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(qnetstats PRIVATE iphlpapi ws2_32)
    if(MSVC)
        target_compile_options(qnetstats PRIVATE /utf-8)
    endif()
endif()

if(APPLE)
    set_target_properties(qnetstats PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME QNetStats
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/dist/macos/Info.plist.in")
    target_sources(qnetstats PRIVATE dist/macos/QNetStats.icns)
    set_source_files_properties(dist/macos/QNetStats.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

install(TARGETS qnetstats RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES dist/com.birdie-github.QNetStats.desktop
            DESTINATION share/applications)
    install(FILES dist/com.birdie-github.QNetStats.appdata.xml
            DESTINATION share/metainfo)
    install(DIRECTORY dist/hicolor
            DESTINATION share/icons)
endif()
