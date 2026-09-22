import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.ToolButton {
    id: control
    implicitWidth: Math.max(44, contentItem.implicitWidth + 24)
    implicitHeight: 44
    leftPadding: 12
    rightPadding: 12
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? Theme.accent : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.hovered || control.down || control.checked ? Theme.hoveredSurface : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent
    }
}
