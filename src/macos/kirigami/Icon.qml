import QtQuick
import QtQuick.Effects
import QtQuick.Window

Item {
	id: root

	property var source
	property string fallback: "application-x-executable"
	property string placeholder: "image-missing"
	property color color: "transparent"
	property bool isMask: false
	property bool roundToIconSize: true
	property bool active: false
	property bool selected: false
	property bool animated: false

	readonly property bool valid: image.status === Image.Ready
	readonly property int status: image.status
	readonly property real paintedWidth: image.paintedWidth
	readonly property real paintedHeight: image.paintedHeight

	readonly property string sourceText: {
		if (root.source === undefined || root.source === null) return "";
		return root.source.toString();
	}
	readonly property bool usesColorization: root.color.a > 0
		&& (root.isMask || root.sourceText.endsWith("-symbolic"))
	property bool usingFallback: false

	implicitWidth: 32
	implicitHeight: 32

	function urlForIcon(name: string): string {
		if (name.length === 0) return "";
		if (name.includes("://") || name.startsWith("qrc:") || name.startsWith("data:")) return name;
		if (name.startsWith("/")) return "file://" + name;
		return "image://icon/" + name;
	}

	onSourceTextChanged: root.usingFallback = false

	Image {
		id: image

		anchors.fill: parent
		visible: !root.usesColorization
		asynchronous: true
		smooth: true
		mipmap: true
		fillMode: Image.PreserveAspectFit
		sourceSize: Qt.size(Math.max(1, root.width) * Screen.devicePixelRatio, Math.max(1, root.height) * Screen.devicePixelRatio)
		source: root.urlForIcon(root.usingFallback ? root.fallback : root.sourceText)

		onStatusChanged: {
			if (status === Image.Error && !root.usingFallback && root.fallback.length > 0) root.usingFallback = true;
		}
	}

	MultiEffect {
		anchors.fill: image
		visible: root.usesColorization
		source: image
		colorization: 1
		colorizationColor: root.color
		brightness: 1
	}
}
