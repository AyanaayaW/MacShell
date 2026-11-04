qt_add_library(quickshell-syntaxhighlighting-compat STATIC syntaxhighlighting.cpp)
qt_add_qml_module(quickshell-syntaxhighlighting-compat
	URI org.kde.syntaxhighlighting
	VERSION 1.0
	DEPENDENCIES QtQuick
)
install_qml_module(quickshell-syntaxhighlighting-compat)
target_link_libraries(quickshell-syntaxhighlighting-compat PRIVATE Qt::Quick Qt::Gui)
target_link_libraries(quickshell PRIVATE quickshell-syntaxhighlighting-compatplugin)
