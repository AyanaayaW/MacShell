qt_add_library(quickshell-service-polkit STATIC polkit.cpp)
qt_add_qml_module(quickshell-service-polkit
	URI Quickshell.Services.Polkit
	VERSION 0.1
	DEPENDENCIES QtQml
)
qs_add_module_deps_light(quickshell-service-polkit quickshell-core)
install_qml_module(quickshell-service-polkit)
target_link_libraries(quickshell-service-polkit PRIVATE Qt::Qml)
target_link_libraries(quickshell PRIVATE quickshell-service-polkitplugin)

qt_add_library(quickshell-service-greetd STATIC greetd.cpp)
qt_add_qml_module(quickshell-service-greetd
	URI Quickshell.Services.Greetd
	VERSION 0.1
	DEPENDENCIES QtQml
)
qs_add_module_deps_light(quickshell-service-greetd quickshell-core)
install_qml_module(quickshell-service-greetd)
target_link_libraries(quickshell-service-greetd PRIVATE Qt::Qml)
target_link_libraries(quickshell PRIVATE quickshell-service-greetdplugin)

qt_add_library(quickshell-niri-unavailable STATIC niri.cpp niri.hpp)
qt_add_qml_module(quickshell-niri-unavailable
	URI Quickshell.Niri
	VERSION 0.1
	DEPENDENCIES QtQml
)
qs_add_module_deps_light(quickshell-niri-unavailable quickshell-core)
install_qml_module(quickshell-niri-unavailable)
target_link_libraries(quickshell-niri-unavailable PRIVATE Qt::Qml)
target_link_libraries(quickshell PRIVATE quickshell-niri-unavailableplugin)

qt_add_library(quickshell-dwl-unavailable STATIC dwl.cpp dwl.hpp)
qt_add_qml_module(quickshell-dwl-unavailable
	URI Quickshell.DWL
	VERSION 0.1
	DEPENDENCIES QtQml
)
qs_add_module_deps_light(quickshell-dwl-unavailable quickshell-core)
install_qml_module(quickshell-dwl-unavailable)
target_link_libraries(quickshell-dwl-unavailable PRIVATE Qt::Qml)
target_link_libraries(quickshell PRIVATE quickshell-dwl-unavailableplugin)
