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
)

target_compile_definitions(qtinspectorprobe PRIVATE QLI_PROBE_VERSION="${PROJECT_VERSION}")

target_link_libraries(qtinspectorprobe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Quick
)

# LD_PRELOAD needs a .so with the "lib" prefix; Qt style plugins accept it too.
set_target_properties(qtinspectorprobe PROPERTIES PREFIX "lib")
