pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import "../theme"

Pane {
    id: root
    required property var playbackController
    readonly property bool compact: width < 600
    signal detailRequested
    signal queueRequested
    function timeText(ms) {
        const seconds = Math.floor(Math.max(0, ms) / 1000);
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toString().padStart(2, "0");
    }
    implicitHeight: (compact ? 80 : 116) + (playbackController.errorMessage.length > 0 ? 24 : 0)
    padding: 12
    background: Rectangle {
        color: Theme.surface
        Rectangle { width: parent.width; height: 1; color: Theme.border }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            spacing: root.compact ? 0 : 8
            ItemDelegate {
                id: identityAction
                Layout.fillWidth: true
                Layout.preferredWidth: root.compact ? 120 : 260
                implicitHeight: 56
                padding: 4
                onClicked: root.detailRequested()
                Accessible.name: "打开正在播放：" + root.playbackController.title
                background: Rectangle {
                    color: identityAction.down || identityAction.hovered ? Theme.hoveredSurface : "transparent"
                    radius: Theme.radius
                    border.width: identityAction.visualFocus ? 2 : 0
                    border.color: Theme.accent
                }
                contentItem: RowLayout {
                    spacing: 12
                    CoverImage {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        coverUrl: root.playbackController.currentTrack.coverUrl || ""
                        entityName: root.playbackController.title
                        pixelSize: 100
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: root.playbackController.title
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: root.playbackController.preparing ? "正在准备播放…" : root.playbackController.artist
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            IconButton {
                symbol: "previous"
                visible: !root.compact
                text: "上一首"
                enabled: root.playbackController.queueCount > 0
                onClicked: root.playbackController.previous()
            }
            IconButton {
                primary: true
                implicitWidth: 48
                implicitHeight: 48
                busy: root.playbackController.preparing
                symbol: root.playbackController.errorMessage ? "retry" : root.playbackController.desiredPlaying ? "pause" : "play"
                text: root.playbackController.errorMessage ? "重试播放" : root.playbackController.desiredPlaying ? "暂停" : "播放"
                enabled: root.playbackController.hasCurrentTrack
                onClicked: root.playbackController.togglePlayback()
            }
            IconButton {
                symbol: "next"
                text: "下一首"
                enabled: root.playbackController.queueCount > 0
                onClicked: root.playbackController.next()
            }
            IconButton {
                symbol: "queue"
                text: "播放队列，" + root.playbackController.queueCount + "首"
                onClicked: root.queueRequested()
            }
            Slider {
                visible: root.width > 760
                Layout.preferredWidth: 100
                from: 0
                to: 100
                value: root.playbackController.volume
                onMoved: root.playbackController.volume = value
                Accessible.name: "音量"
            }
        }
        RowLayout {
            visible: !root.compact
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: root.timeText(progress.pressed ? progress.value : root.playbackController.position)
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Slider {
                id: progress
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.playbackController.duration)
                Binding {
                    target: progress
                    property: "value"
                    value: root.playbackController.position
                    when: !progress.pressed
                    restoreMode: Binding.RestoreNone
                }
                enabled: root.playbackController.seekable
                onMoved: if (!pressed) root.playbackController.seek(value)
                onPressedChanged: if (!pressed) root.playbackController.seek(value)
                Accessible.name: "播放进度"
            }
            Label {
                text: root.playbackController.duration > 0 ? root.timeText(root.playbackController.duration) : "—"
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }
        Rectangle {
            visible: root.compact
            Layout.fillWidth: true
            implicitHeight: 2
            color: Theme.border
            Rectangle {
                width: parent.width * Math.min(1, root.playbackController.duration > 0 ? root.playbackController.position / root.playbackController.duration : 0)
                height: parent.height
                color: Theme.accent
            }
        }
        Label {
            visible: root.playbackController.errorMessage.length > 0
            text: root.playbackController.errorMessage
            color: Theme.error
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
