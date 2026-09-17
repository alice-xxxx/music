import QtQuick
import QtQuick.Controls
import "../theme"

Button {
    id: root
    required property string symbol
    property bool primary: false
    property bool busy: false
    implicitWidth: primary ? 64 : 48
    implicitHeight: implicitWidth
    display: AbstractButton.IconOnly
    icon.source: "../icons/" + symbol + ".svg"
    icon.width: root.primary ? 28 : 23
    icon.height: root.primary ? 28 : 23
    icon.color: root.primary ? "white" : !root.enabled ? Theme.textSecondary : root.checked
                                                         ? Theme.accent : Theme.textPrimary
    Accessible.name: text
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: width / 2
        color: root.primary ? Theme.accent : root.down || root.hovered || root.checked
                              ? Theme.hoveredSurface : "transparent"
        opacity: root.down ? 0.72 : 1
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.accent
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: root.primary && root.visualFocus
        }
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
    }
    BusyIndicator {
        anchors.centerIn: parent
        width: parent.width + 8
        height: width
        running: root.busy
        visible: running
    }
}
