import QtQuick
import QtQuick.Controls
import "../theme"

MenuItem {
    id: root
    property bool destructive: false
    implicitHeight: 48
    height: visible ? implicitHeight : 0
    leftPadding: 14
    rightPadding: 14
    contentItem: Label {
        text: root.text
        color: !root.enabled ? Theme.textSecondary :
               root.destructive ? Theme.error : Theme.textPrimary
        opacity: root.enabled ? 1 : 0.55
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 8
        color: root.down || root.highlighted || root.hovered ? Theme.hoveredSurface : "transparent"
    }
}
