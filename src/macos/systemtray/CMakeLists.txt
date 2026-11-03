qt_add_library(quickshell-service-statusnotifier STATIC
	systemtray.mm
)

qt_add_qml_module(quickshell-service-statusnotifier
	URI Quickshell.Services.SystemTray
	VERSION 0.1
	DEPENDENCIES QtQml Quickshell
)

qs_add_module_deps_light(quickshell-service-statusnotifier quickshell-core)

install_qml_module(quickshell-service-statusnotifier)

target_link_libraries(quickshell-service-statusnotifier PRIVATE
	Qt::Quick
	"-framework AppKit"
	"-framework ApplicationServices"
)
target_link_libraries(quickshell PRIVATE quickshell-service-statusnotifierplugin)
