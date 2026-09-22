import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.ComboBox {
    id: control
    implicitHeight: 44
    implicitWidth: Math.max(160, contentItem.implicitWidth + 48)
    leftPadding: 14
    rightPadding: 36
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Item {
        width: 20
        height: 20
        x: control.width - width - 12
        y: (control.height - height) / 2
        Rectangle { x: 4; y: 9; width: 8; height: 2; radius: 1; rotation: 45; color: Theme.textSecondary }
        Rectangle { x: 9; y: 9; width: 8; height: 2; radius: 1; rotation: -45; color: Theme.textSecondary }
    }
    background: Rectangle {
        radius: Theme.radius
        color: control.hovered || control.down ? Theme.hoveredSurface : Theme.surface
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
    }
}
