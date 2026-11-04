qt_add_library(quickshell-macos-compat STATIC
	commands.mm
	devices.mm
	nmcli.mm
)

target_link_libraries(quickshell-macos-compat PRIVATE
	Qt::Core
	quickshell-service-pipewire
	"-framework AppKit"
	"-framework Carbon"
	"-framework CoreGraphics"
	"-framework CoreWLAN"
	"-framework Network"
	"-framework UniformTypeIdentifiers"
)
target_link_libraries(quickshell PRIVATE quickshell-macos-compat)
