import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.Button {
    id: control
    implicitHeight: 44
    implicitWidth: Math.max(80, contentItem.implicitWidth + 32)
    leftPadding: 16
    rightPadding: 16
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.highlighted ? Theme.accentText : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.highlighted ? Theme.accent : control.down || control.hovered
               ? Theme.hoveredSurface : control.flat ? "transparent" : Theme.surface
        border.width: control.visualFocus ? 2 : control.flat || control.highlighted ? 0 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
        opacity: control.down ? 0.8 : 1
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
