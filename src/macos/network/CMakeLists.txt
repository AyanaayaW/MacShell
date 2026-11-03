set(UPSTREAM_NETWORK_DIRECTORY ${CMAKE_SOURCE_DIR}/src/network)

qt_add_library(quickshell-network STATIC
	${UPSTREAM_NETWORK_DIRECTORY}/network.cpp
	${UPSTREAM_NETWORK_DIRECTORY}/device.cpp
	${UPSTREAM_NETWORK_DIRECTORY}/wired.cpp
	${UPSTREAM_NETWORK_DIRECTORY}/wifi.cpp
	${UPSTREAM_NETWORK_DIRECTORY}/enums.cpp
	${UPSTREAM_NETWORK_DIRECTORY}/qml.cpp
	${UPSTREAM_NETWORK_DIRECTORY}/network.hpp
	${UPSTREAM_NETWORK_DIRECTORY}/device.hpp
	${UPSTREAM_NETWORK_DIRECTORY}/wired.hpp
	${UPSTREAM_NETWORK_DIRECTORY}/wifi.hpp
	${UPSTREAM_NETWORK_DIRECTORY}/enums.hpp
	${UPSTREAM_NETWORK_DIRECTORY}/qml.hpp
	nm_settings.hpp
	nm_settings.cpp
	corewlan_backend.hpp
	corewlan_backend.mm
)

target_include_directories(quickshell-network PRIVATE ${UPSTREAM_NETWORK_DIRECTORY} ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_qml_module(quickshell-network
	URI Quickshell.Networking
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-network quickshell-core)

install_qml_module(quickshell-network)

target_link_libraries(quickshell-network PRIVATE
	Qt::Qml
	"-framework CoreWLAN"
	"-framework CoreLocation"
	"-framework Network"
	"-framework Foundation"
)
target_link_libraries(quickshell PRIVATE quickshell-networkplugin)
