find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)
if(COMMAND qt_policy)
  qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(clipper2next_qml_demo
  src/main.cpp
  src/demo_controller.cpp
  src/demo_controller.h
  src/demo_model.cpp
  src/demo_model.h
  src/path_adapter.cpp
  src/path_adapter.h
)

target_link_libraries(clipper2next_qml_demo
  PRIVATE
    clipper2next::clipper2next
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

target_compile_features(clipper2next_qml_demo PRIVATE cxx_std_23)
target_include_directories(clipper2next_qml_demo PRIVATE src)
clipper2next_target_warnings(clipper2next_qml_demo)
clipper2next_target_sanitizers(clipper2next_qml_demo)

set_source_files_properties(qml/AppTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(clipper2next_qml_demo
  URI Clipper2NextDemo
  VERSION 1.0
  QML_FILES
    qml/AppTheme.qml
    qml/CommandBar.qml
    qml/DesignButton.qml
    qml/GeometryCanvas.qml
    qml/InspectorPanel.qml
    qml/Main.qml
    qml/MetricsBar.qml
    qml/MetricTile.qml
    qml/NumberBox.qml
)

set_target_properties(clipper2next_qml_demo PROPERTIES
  FOLDER Demos
  WIN32_EXECUTABLE TRUE
)
