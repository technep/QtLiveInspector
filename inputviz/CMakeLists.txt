add_library(qtinspectorinputviz MODULE
    inputviz_main.cpp
    ../probe/InputVisualizer.h
    ../probe/InputVisualizer.cpp
)

target_include_directories(qtinspectorinputviz PRIVATE ../probe)
target_link_libraries(qtinspectorinputviz PRIVATE
    qli::warnings
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
)

set_target_properties(qtinspectorinputviz PROPERTIES PREFIX "lib")
