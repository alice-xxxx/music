import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import "../theme"

Controls.Dialog {
    id: control
    padding: 20
    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.color: Theme.border
    }
    header: Item {
        implicitHeight: Math.max(64, titleLabel.implicitHeight + 24)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 8
            Controls.Label {
                id: titleLabel
                text: control.title
                font.pixelSize: Theme.sectionTitleSize
                font.bold: true
                color: Theme.textPrimary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            IconButton {
                symbol: "close"
                text: "关闭弹窗"
                onClicked: control.reject()
            }
        }
    }
}
