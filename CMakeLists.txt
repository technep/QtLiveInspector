cmake_minimum_required(VERSION 3.19)
project(QtLiveInspector VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(QLI_BUILD_SAMPLES "Build the sample target application" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Quick QuickControls2)

add_subdirectory(probe)
add_subdirectory(inspector)
if(QLI_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

message(STATUS "")
message(STATUS "QtLiveInspector ${PROJECT_VERSION}")
message(STATUS "  probe     : libqtinspectorprobe (inject into the target Qt6 app)")
message(STATUS "  inspector : qtliveinspector (GUI client, local & remote)")
message(STATUS "")
