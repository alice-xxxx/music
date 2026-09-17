pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Pane {
    id: root
    required property var playbackController

    signal detailRequested
    signal queueRequested
    implicitHeight: root.width < 600 ? (root.playbackController.errorMessage.length > 0 ? 100 : 76) :
                                       112
    height: implicitHeight
    padding: 12
    background: Rectangle {
        color: Theme.surface
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            ItemDelegate {
                id: identityAction
                Layout.fillWidth: true
                Layout.preferredWidth: root.width >= 600 ? 240 : 120
                implicitHeight: 56
                onClicked: root.detailRequested()
                Accessible.name: "打开正在播放：" + root.playbackController.title
                background: Rectangle {
                    color: identityAction.down || identityAction.hovered ? Theme.hoveredSurface :
                                                                          "transparent"
                    radius: 8
                    border.width: identityAction.visualFocus ? 1 : 0
                    border.color: Theme.accent
                }
                contentItem: RowLayout {
                    spacing: 10
                    CoverImage {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        coverUrl: root.playbackController.currentTrack.coverUrl || ""
                        entityName: root.playbackController.title
                        pixelSize: 100
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: root.playbackController.hasCurrentTrack ?
                                      root.playbackController.title : "尚未选择歌曲"
                            font.bold: root.playbackController.hasCurrentTrack
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: root.playbackController.artist
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            IconButton {
                symbol: "previous"
                visible: root.width >= 600
                text: "上一首"
                enabled: root.playbackController.queueCount > 0
                onClicked: root.playbackController.previous()
            }
            IconButton {
                symbol: root.playbackController.errorMessage ? "retry" :
                                                               root.playbackController.desiredPlaying
                                                               ? "pause" : "play"
                text: root.playbackController.errorMessage.length > 0 ? "重试播放" :
                                                                        root.playbackController.desiredPlaying
                                                                        ? "暂停" : "播放"
                enabled: root.playbackController.hasCurrentTrack
                onClicked: root.playbackController.togglePlayback()
                Accessible.name: text
            }
            IconButton {
                symbol: "next"
                visible: root.width >= 600
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
                Layout.preferredWidth: 110
                from: 0
                to: 100
                value: root.playbackController.volume
                onMoved: root.playbackController.volume = value
                Accessible.name: "音量"
            }
        }
        Slider {
            id: progress
            visible: root.width >= 600
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
            onPressedChanged: if (!pressed)
                                  root.playbackController.seek(value)
            Accessible.name: "播放进度"
        }
        Rectangle {
            visible: root.width < 600
            Layout.fillWidth: true
            implicitHeight: 2
            color: Theme.border
            Rectangle {
                width: parent.width * Math.min(1, root.playbackController.duration > 0 ?
                                                   root.playbackController.position /
                                                   root.playbackController.duration : 0)
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
