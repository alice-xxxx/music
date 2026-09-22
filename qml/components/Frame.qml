import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.Frame {
    padding: 20
    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.color: Theme.border
    }
}
