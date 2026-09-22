import QtQuick
import QtQuick.Controls
import "../theme"

TabButton {
    id: root
    property bool sideNavigation: false
    implicitHeight: 48
    contentItem: Text {
        text: root.text
        font.pixelSize: root.font.pixelSize
        font.bold: root.checked
        color: root.checked ? Theme.accent : Theme.textSecondary
        horizontalAlignment: root.sideNavigation ? Text.AlignLeft : Text.AlignHCenter
        leftPadding: root.sideNavigation ? 16 : 0
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: Theme.radius
        color: root.down || root.hovered || (root.sideNavigation && root.checked) ? Theme.hoveredSurface : "transparent"
        border.width: root.visualFocus ? 1 : 0
        border.color: Theme.accent
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: 24
            height: 3
            radius: 1.5
            color: Theme.accent
            visible: root.checked && !root.sideNavigation
        }
    }
}
