qt_add_library(quickshell-bluetooth STATIC
	bluetooth_backend.mm
	bluetooth.cpp
)

qt_add_qml_module(quickshell-bluetooth
	URI Quickshell.Bluetooth
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-bluetooth quickshell-core)

install_qml_module(quickshell-bluetooth)

target_link_libraries(quickshell-bluetooth PRIVATE
	Qt::Qml
	"-framework IOBluetooth"
	"-framework IOKit"
	"-framework Foundation"
)
target_link_libraries(quickshell PRIVATE quickshell-bluetoothplugin)
