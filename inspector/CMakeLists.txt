add_executable(qtliveinspector
    main.cpp
    MainWindow.h
    MainWindow.cpp
    RemoteConnection.h
    RemoteConnection.cpp
    ObjectTreeModel.h
    ObjectTreeModel.cpp
    PropertyModel.h
    PropertyModel.cpp
)

target_link_libraries(qtliveinspector PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)
