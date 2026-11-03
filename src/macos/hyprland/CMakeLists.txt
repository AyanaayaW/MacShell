qt_add_library(quickshell-hyprland STATIC
	spaces_backend.mm
	hyprland.cpp
	focus_grab.mm
	global_shortcuts.mm
	hyprland_window.cpp
)

qt_add_qml_module(quickshell-hyprland
	URI Quickshell.Hyprland
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-hyprland quickshell-core quickshell-window)

install_qml_module(quickshell-hyprland)

target_link_libraries(quickshell-hyprland PRIVATE
	Qt::Quick
	"-framework AppKit"
	"-framework ApplicationServices"
	"-framework CoreGraphics"
	"-framework Carbon"
)
target_link_libraries(quickshell PRIVATE quickshell-hyprlandplugin)
