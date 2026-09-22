import QtQuick
import QtQuick.Controls as Controls
import "../theme"

Controls.Slider {
    id: control
    implicitHeight: 32
    implicitWidth: 160
    leftPadding: 8
    rightPadding: 8
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.border
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.pressed || control.visualFocus ? 16 : 12
        height: width
        radius: width / 2
        color: Theme.accent
        visible: control.enabled
    }
}
