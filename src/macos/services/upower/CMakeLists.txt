qt_add_library(quickshell-service-upower STATIC
	power_sources.cpp
	upower.cpp
	power_profiles.mm
)

qt_add_qml_module(quickshell-service-upower
	URI Quickshell.Services.UPower
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-upower quickshell-core)

install_qml_module(quickshell-service-upower)

target_link_libraries(quickshell-service-upower PRIVATE
	Qt::Qml
	"-framework IOKit"
	"-framework CoreFoundation"
	"-framework Foundation"
)
target_link_libraries(quickshell PRIVATE quickshell-service-upowerplugin)
