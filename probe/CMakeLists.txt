add_library(qtinspectorprobe MODULE
    probe_main.cpp
    ProbeController.h
    ProbeController.cpp
    ObjectRegistry.h
    ObjectTree.h
    ObjectTree.cpp
    JsonUtil.h
    JsonUtil.cpp
    NotifyRelay.h
    Highlighter.h
    Highlighter.cpp
    Picker.h
    Picker.cpp
    SquishLocator.h
    SquishLocator.cpp
    InputVisualizer.h
    InputVisualizer.cpp
    QuickTouchOverlay.h
    PerfMonitor.h
    PerfMonitor.cpp
)

target_compile_definitions(qtinspectorprobe PRIVATE QLI_PROBE_VERSION="${PROJECT_VERSION}")

target_link_libraries(qtinspectorprobe PRIVATE
    qli::warnings
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Quick
)

set_target_properties(qtinspectorprobe PROPERTIES PREFIX "lib")
