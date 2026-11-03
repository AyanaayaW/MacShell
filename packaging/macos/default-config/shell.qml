import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland
import Quickshell.Services.UPower
import Quickshell.Services.Pipewire
import Quickshell.Services.Mpris
import Quickshell.Networking
import Quickshell.Bluetooth
import QtQuick
import QtQuick.Layouts

Scope {
  id: root

  readonly property color backgroundColor: "#1e1e2e"
  readonly property color surfaceColor: "#313244"
  readonly property color textColor: "#cdd6f4"
  readonly property color accentColor: "#89b4fa"
  readonly property color dimColor: "#6c7086"

  readonly property var sink: Pipewire.defaultAudioSink
  readonly property var battery: UPower.displayDevice
  readonly property var player: Mpris.players.values.length > 0 ? Mpris.players.values[0] : null
  readonly property var wifiDevice: Networking.devices.values.find(device => device.type === DeviceType.Wifi) ?? null
  readonly property var connectedWifi: wifiDevice ? wifiDevice.networks.values.find(network => network.connected) ?? null : null
  readonly property int connectedBluetoothCount: Bluetooth.devices.values.filter(device => device.connected).length

  PwObjectTracker { objects: [root.sink] }

  SystemClock { id: clock; precision: SystemClock.Seconds }

  component StatusChip: Rectangle {
    property alias iconName: chipIcon.source
    property alias label: chipLabel.text
    implicitHeight: 22
    implicitWidth: chipRow.implicitWidth + 16
    radius: 11
    color: root.surfaceColor

    RowLayout {
      id: chipRow
      anchors.centerIn: parent
      spacing: 6
      Image {
        id: chipIcon
        Layout.preferredWidth: 14
        Layout.preferredHeight: 14
        sourceSize.width: 28
        sourceSize.height: 28
      }
      Text {
        id: chipLabel
        color: root.textColor
        font.pixelSize: 12
      }
    }
  }

  Variants {
    model: Quickshell.screens

    PanelWindow {
      required property var modelData
      screen: modelData

      anchors { top: true; left: true; right: true }
      implicitHeight: 32
      color: root.backgroundColor
      WlrLayershell.layer: WlrLayer.Top
      WlrLayershell.namespace: "macshell-demo"

      RowLayout {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Repeater {
          model: Hyprland.workspaces.values
          Rectangle {
            required property var modelData
            implicitWidth: modelData.focused ? 28 : 22
            implicitHeight: 22
            radius: 11
            color: modelData.focused ? root.accentColor : root.surfaceColor
            Text {
              anchors.centerIn: parent
              text: modelData.name
              color: modelData.focused ? root.backgroundColor : root.textColor
              font.bold: true
              font.pixelSize: 12
            }
            MouseArea { anchors.fill: parent; onClicked: modelData.activate() }
          }
        }

        StatusChip {
          visible: root.player !== null
          iconName: Quickshell.iconPath(root.player && root.player.isPlaying ? "media-playback-start" : "media-playback-pause")
          label: root.player ? root.player.trackTitle + " — " + root.player.trackArtist : ""
        }
      }

      Text {
        anchors.centerIn: parent
        text: Qt.formatDateTime(clock.date, "ddd d MMM   hh:mm:ss")
        color: root.textColor
        font.bold: true
        font.pixelSize: 13
      }

      RowLayout {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        StatusChip {
          iconName: Quickshell.iconPath(root.connectedWifi ? "network-wireless-signal-excellent-symbolic" : "network-wireless-offline-symbolic")
          label: root.connectedWifi ? root.connectedWifi.name : (Networking.wifiEnabled ? "Wi-Fi" : "Off")
        }

        StatusChip {
          iconName: Quickshell.iconPath(Bluetooth.defaultAdapter && Bluetooth.defaultAdapter.enabled ? "bluetooth-active-symbolic" : "bluetooth-disabled-symbolic")
          label: root.connectedBluetoothCount > 0 ? root.connectedBluetoothCount + " connected" : "On"
        }

        StatusChip {
          iconName: Quickshell.iconPath(root.sink && root.sink.audio.muted ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic")
          label: root.sink ? Math.round(root.sink.audio.volume * 100) + "%  " + root.sink.description : "No output"
        }

        StatusChip {
          visible: root.battery.isLaptopBattery
          iconName: root.battery.iconName ? Quickshell.iconPath(root.battery.iconName) : ""
          label: Math.round(root.battery.percentage * 100) + "%"
        }
      }
    }
  }
}
