cmake_minimum_required(VERSION 3.19)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Tool-dependent tests below (smoke run, probe integration) import the CLI
# tools tree; they are only registered when it is part of the checkout.
set(QLI_HAVE_TOOLS FALSE)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/probeclient.py)
    set(QLI_HAVE_TOOLS TRUE)
endif()

# -- C++ unit tests ----------------------------------------------------------

add_executable(test_jsonutil
    test_jsonutil.cpp
    ../probe/JsonUtil.cpp
)

target_include_directories(test_jsonutil PRIVATE ../probe)
target_link_libraries(test_jsonutil PRIVATE qli::warnings Qt6::Core Qt6::Gui Qt6::Test)

add_test(NAME JsonUtilTest COMMAND test_jsonutil)
# QTEST_MAIN pulls in QGuiApplication (Qt6::Gui is linked), which needs a
# platform plugin; headless CI has no display. Same offscreen treatment as
# InputVisualizerTest below.
set_tests_properties(JsonUtilTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# State-machine tests for the input visualizer (press / press-and-hold /
# release / drag / multi-touch) — events are injected via the application
# event filter, no window or input hardware required.
add_executable(test_inputvisualizer
    test_inputvisualizer.cpp
    ../probe/InputVisualizer.cpp
)

target_include_directories(test_inputvisualizer PRIVATE ../probe)
target_link_libraries(test_inputvisualizer PRIVATE
    qli::warnings
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Test)

add_test(NAME InputVisualizerTest COMMAND test_inputvisualizer)
set_tests_properties(InputVisualizerTest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# -- Sanitizer support ---------------------------------------------------------
# When the project is configured with -fsanitize=address, the integration
# tests must LD_PRELOAD the ASan runtime BEFORE the probe library, otherwise
# the target dies at startup ("ASan runtime does not come first").
set(QLI_TEST_PRELOAD_EXTRA "")
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=address")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libasan.so
        OUTPUT_VARIABLE QLI_LIBASAN OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(QLI_LIBASAN AND NOT QLI_LIBASAN STREQUAL "libasan.so"
       AND EXISTS ${QLI_LIBASAN})
        string(APPEND QLI_TEST_PRELOAD_EXTRA "${QLI_LIBASAN}")
    endif()
endif()
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=undefined")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libubsan.so
        OUTPUT_VARIABLE QLI_LIBUBSAN OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(QLI_LIBUBSAN AND NOT QLI_LIBUBSAN STREQUAL "libubsan.so"
       AND EXISTS ${QLI_LIBUBSAN})
        string(APPEND QLI_TEST_PRELOAD_EXTRA ":${QLI_LIBUBSAN}")
    endif()
endif()

# -- Python tool smoke tests (no Qt application needed) ----------------------

if(QLI_HAVE_TOOLS)
    add_test(NAME ToolSmokeTest
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/smoke_tools.py)
    set_tests_properties(ToolSmokeTest PROPERTIES TIMEOUT 120)
endif()

# -- Probe integration tests (real probe injected into the sample app) -------

if(QLI_HAVE_TOOLS AND TARGET quickdemo AND TARGET qtinspectorprobe)
    # Ports are fixed (not ephemeral) so the runner and client agree;
    # RUN_SERIAL keeps the three runs from colliding.
    add_test(NAME ProbeIntegration_basic
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/integration_probe.py
            --demo $<TARGET_FILE:quickdemo>
            --probe-so $<TARGET_FILE:qtinspectorprobe>
            --port 17317 --mode basic)
    add_test(NAME ProbeIntegration_readonly
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/integration_probe.py
            --demo $<TARGET_FILE:quickdemo>
            --probe-so $<TARGET_FILE:qtinspectorprobe>
            --port 17318 --mode readonly)
    add_test(NAME ProbeIntegration_noauth
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/integration_probe.py
            --demo $<TARGET_FILE:quickdemo>
            --probe-so $<TARGET_FILE:qtinspectorprobe>
            --port 17319 --mode noauth)
    set_tests_properties(ProbeIntegration_basic ProbeIntegration_readonly
        ProbeIntegration_noauth PROPERTIES
        RUN_SERIAL TRUE TIMEOUT 90
        ENVIRONMENT "QLI_TEST_PRELOAD_EXTRA=${QLI_TEST_PRELOAD_EXTRA}")

    # Same protocol surface, different target flavors: a Qt Quick scene in a
    # plain QQuickView, and a pure Qt Widgets app (widget overlay paths).
    add_test(NAME ProbeIntegration_quickview
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/integration_probe.py
            --demo $<TARGET_FILE:quickviewdemo>
            --probe-so $<TARGET_FILE:qtinspectorprobe>
            --port 17321 --mode basic --profile quickview)
    add_test(NAME ProbeIntegration_widgets
        COMMAND Python3::Interpreter
            ${CMAKE_CURRENT_SOURCE_DIR}/integration_probe.py
            --demo $<TARGET_FILE:widgetdemo>
            --probe-so $<TARGET_FILE:qtinspectorprobe>
            --port 17320 --mode basic --profile widgets)
    set_tests_properties(ProbeIntegration_quickview ProbeIntegration_widgets
        PROPERTIES
        RUN_SERIAL TRUE TIMEOUT 90
        ENVIRONMENT "QLI_TEST_PRELOAD_EXTRA=${QLI_TEST_PRELOAD_EXTRA}")
endif()
