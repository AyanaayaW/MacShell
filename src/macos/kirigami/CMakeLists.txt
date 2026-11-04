qt_add_library(quickshell-kirigami-compat STATIC)
qt_add_qml_module(quickshell-kirigami-compat
	URI org.kde.kirigami
	VERSION 2.0
	QML_FILES Icon.qml
	DEPENDENCIES QtQuick
)
install_qml_module(quickshell-kirigami-compat)
target_link_libraries(quickshell-kirigami-compat PRIVATE Qt::Quick)
target_link_libraries(quickshell PRIVATE quickshell-kirigami-compatplugin)
