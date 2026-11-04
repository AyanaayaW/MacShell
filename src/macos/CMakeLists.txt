add_compile_options($<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

qt_add_library(quickshell-macos STATIC
	panel_window.cpp
	cocoa.mm
	icons.mm
	applications.mm
	idle.cpp
	toplevels_backend.mm
	toplevels.cpp
	session_lock.cpp
	exclusive_zones.mm
	background_effect.cpp
	shortcut_inhibitor.cpp
	screencopy.mm
	settings.mm
	linux_files.mm
)

qt_add_qml_module(quickshell-macos
	URI Quickshell.Wayland
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-macos quickshell-core quickshell-window)

install_qml_module(quickshell-macos)

add_library(quickshell-macos-init OBJECT init.cpp)

target_link_libraries(quickshell-macos PRIVATE Qt::Quick "-framework AppKit" "-framework IOKit" "-framework CoreGraphics" "-framework ApplicationServices" "-framework ScreenCaptureKit" "-framework CoreMedia" "-framework CoreVideo" "-framework ServiceManagement")
target_link_libraries(quickshell-macos-init PRIVATE Qt::Quick Qt::Qml)

target_link_libraries(quickshell PRIVATE quickshell-macosplugin quickshell-macos-init "-framework AppKit")

add_subdirectory(services/upower)
add_subdirectory(services/pipewire)
add_subdirectory(hyprland)
add_subdirectory(bluetooth)

target_link_options(quickshell PRIVATE
	"LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/embedded-info.plist"
)
add_subdirectory(network)
add_subdirectory(mpris)
add_subdirectory(systemtray)
add_subdirectory(unavailable)
add_subdirectory(i3)
add_subdirectory(compat)
