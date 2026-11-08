cmake_minimum_required(VERSION 3.19)
project(QtLiveInspector VERSION 0.3.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The Python tools carry their own copy of the version (tools/qli_version.py)
# because they may run from source; fail configure loudly when it drifts from
# the CMake project version.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/tools/qli_version.py QLI_VERSION_PY)
string(REGEX MATCH "__version__ = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _qli_ver
       "${QLI_VERSION_PY}")
if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "tools/qli_version.py (${CMAKE_MATCH_1}) does not match the CMake "
        "project version (${PROJECT_VERSION})")
endif()

option(QLI_BUILD_SAMPLES "Build the sample target application" ON)
option(QLI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)

# -Wall -Wextra on all first-party targets; CI configures
# -DQLI_WARNINGS_AS_ERRORS=ON. Kept off by default so a newer compiler's
# fresh warnings don't break downstream builds.
add_library(qli_warnings INTERFACE)
if(MSVC)
    target_compile_options(qli_warnings INTERFACE /W4
        $<$<BOOL:${QLI_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(qli_warnings INTERFACE -Wall -Wextra
        $<$<BOOL:${QLI_WARNINGS_AS_ERRORS}>:-Werror>)
endif()
add_library(qli::warnings ALIAS qli_warnings)

enable_testing()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Quick QuickControls2 Test)

add_subdirectory(probe)
add_subdirectory(inputviz)
add_subdirectory(inspector)
if(QLI_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
add_subdirectory(tests)

# -- installation ------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS qtinspectorprobe
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qtliveinspector
    COMPONENT probe)
install(TARGETS qtinspectorinputviz
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qtliveinspector
    COMPONENT probe)
install(TARGETS qtliveinspector
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT inspector)
install(PROGRAMS
    tools/qli_cli.py
    tools/qli_mcp.py
    tools/qli_monkey.py
    tools/qli_bdd.py
    tools/probeclient.py
    tools/qli_version.py
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT tools)
install(FILES protocol.txt README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT docs)

# -- packaging ----------------------------------------------------------------
# `cmake --build . --target package` always produces a tarball; a .deb is
# added with -DQLI_PACKAGE_DEB=ON on systems where Qt comes from the distro
# (library deps are scraped automatically via dpkg-shlibdeps, which cannot
# resolve non-system Qt installations such as the official Qt online
# installer).
option(QLI_PACKAGE_DEB "Also generate a .deb via CPack" OFF)
set(CPACK_PACKAGE_VENDOR "QtLiveInspector contributors")
set(CPACK_PACKAGE_CONTACT "QtLiveInspector contributors <neptech@neupane.de>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Live inspector, probe and input visualizer for Qt6 applications")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/QtLiveInspector/QtLiveInspector")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ")
find_program(DPKG_SHLIBDEPS dpkg-shlibdeps)
if(QLI_PACKAGE_DEB AND DPKG_SHLIBDEPS)
    list(APPEND CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
elseif(QLI_PACKAGE_DEB)
    message(WARNING "QLI_PACKAGE_DEB=ON but dpkg-shlibdeps not found; .deb disabled")
endif()
include(CPack)

message(STATUS "")
message(STATUS "QtLiveInspector ${PROJECT_VERSION}")
message(STATUS "  probe     : libqtinspectorprobe (inject into the target Qt6 app)")
message(STATUS "  inspector : qtliveinspector (GUI client, local & remote)")
message(STATUS "")
