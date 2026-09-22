import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.Dialog {
    id: control
    padding: 20
    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.color: Theme.border
    }
    header: Controls.Label {
        text: control.title
        visible: text.length > 0
        font.pixelSize: Theme.sectionTitleSize
        font.bold: true
        color: Theme.textPrimary
        padding: 20
        bottomPadding: 8
        wrapMode: Text.Wrap
    }
}
