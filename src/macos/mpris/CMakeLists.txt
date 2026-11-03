qt_add_library(quickshell-service-mpris STATIC
	media_sources.mm
	mpris.cpp
)

qt_add_qml_module(quickshell-service-mpris
	URI Quickshell.Services.Mpris
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-mpris quickshell-core)

install_qml_module(quickshell-service-mpris)

target_link_libraries(quickshell-service-mpris PRIVATE
	Qt::Qml
	"-framework AppKit"
	"-framework ScriptingBridge"
	"-framework Foundation"
)
target_link_libraries(quickshell PRIVATE quickshell-service-mprisplugin)
