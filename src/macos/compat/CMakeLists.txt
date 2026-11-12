qt_add_library(quickshell-macos-compat STATIC
	commands.mm
	devices.mm
	nmcli.mm
	tools.mm
	linux_install.cpp
	linux_install_parser.cpp
)

target_link_libraries(quickshell-macos-compat PRIVATE
	Qt::Core
	quickshell-service-pipewire
	"-framework AppKit"
	"-framework Carbon"
	"-framework CoreGraphics"
	"-framework CoreLocation"
	"-framework CoreWLAN"
	"-framework Network"
	"-framework UniformTypeIdentifiers"
)
target_link_libraries(quickshell PRIVATE quickshell-macos-compat)

if (BUILD_TESTING)
	add_executable(linux_install_parser_test test/linux_install_parser.cpp linux_install_parser.cpp)
	target_link_libraries(linux_install_parser_test PRIVATE Qt::Core Qt::Test)
	add_test(NAME linux_install_parser COMMAND $<TARGET_FILE:linux_install_parser_test>)
endif()
