add_executable(widgetdemo main.cpp)

target_link_libraries(widgetdemo PRIVATE
    qli::warnings
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
