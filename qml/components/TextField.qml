import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.TextField {
    id: control
    implicitHeight: 48
    leftPadding: 16
    rightPadding: 16
    color: Theme.textPrimary
    placeholderTextColor: Theme.textSecondary
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
