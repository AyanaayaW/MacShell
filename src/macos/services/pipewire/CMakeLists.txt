qt_add_library(quickshell-service-pipewire STATIC
	core_audio.cpp
	level_meter.mm
	pipewire.cpp
	spectrum.mm
)

qt_add_qml_module(quickshell-service-pipewire
	URI Quickshell.Services.Pipewire
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-pipewire quickshell-core)

install_qml_module(quickshell-service-pipewire)

target_link_libraries(quickshell-service-pipewire PRIVATE
	Qt::Qml
	"-framework CoreAudio"
	"-framework AudioToolbox"
	"-framework Accelerate"
	"-framework CoreFoundation"
)
target_link_libraries(quickshell PRIVATE quickshell-service-pipewireplugin)
