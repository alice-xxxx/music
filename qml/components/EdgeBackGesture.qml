pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: root
    property bool fromRight: false
    property bool canceled: false
    readonly property real distance: drag.activeTranslation.x * (fromRight ? -1 : 1)
    signal backRequested()
    width: 20
    DragHandler {
        id: drag
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        dragThreshold: 24
        onCanceled: root.canceled = true
        onActiveChanged: {
            if (active) {
                root.canceled = false;
            } else if (!root.canceled && root.enabled && root.distance >= 80) {
                root.backRequested();
            }
        }
    }
    Rectangle {
        visible: drag.active && root.distance > 24
        width: 40
        height: 40
        radius: 20
        x: root.fromRight ? -width : root.width
        y: Math.max(8, Math.min(root.height - height - 8, drag.centroid.position.y - height / 2))
        color: Theme.surface
        border.color: Theme.border
        opacity: Math.min(1, root.distance / 80)
        Text {
            anchors.centerIn: parent
            text: root.fromRight ? "›" : "‹"
            font.pixelSize: 28
            color: Theme.accent
        }
    }
}
