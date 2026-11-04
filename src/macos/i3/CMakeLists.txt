qt_add_library(quickshell-i3 STATIC
	i3.cpp
)

qt_add_qml_module(quickshell-i3
	URI Quickshell.I3
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-i3 quickshell-core)

install_qml_module(quickshell-i3)

target_include_directories(quickshell-i3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../hyprland)
target_link_libraries(quickshell-i3 PRIVATE Qt::Quick quickshell-hyprland)
target_link_libraries(quickshell PRIVATE quickshell-i3plugin)
