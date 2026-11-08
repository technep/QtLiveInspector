qt_add_executable(quickviewdemo main.cpp)

qt_add_qml_module(quickviewdemo
    URI QuickViewDemo
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(quickviewdemo PRIVATE
    qli::warnings
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)
