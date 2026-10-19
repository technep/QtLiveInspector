qt_add_executable(quickdemo main.cpp)

qt_add_qml_module(quickdemo
    URI QuickDemo
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(quickdemo PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)
