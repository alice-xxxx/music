pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Drawer {
    id: root
    required property var player
    edge: parent && parent.width < 600 ? Qt.BottomEdge : Qt.RightEdge
    width: edge === Qt.RightEdge ? Math.min(parent.width, 400) : parent.width
    height: edge === Qt.BottomEdge ? Math.min(parent.height * 0.8, 640) : parent.height

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "播放队列 · " + root.player.queueCount
                    font.pixelSize: 20
                    font.bold: true
                }
                Label {
                    text: ["顺序播放", "列表循环", "单曲循环"][root.player.repeatMode] +
                          (root.player.shuffle ? " · 随机" : "")
                    color: Theme.textSecondary
                }
            }
            ToolButton {
                text: "管理"
                enabled: root.player.queueCount > 0
                onClicked: queueActions.open()
                Menu {
                    id: queueActions
                    MenuItem {
                        text: "清空播放队列"
                        onTriggered: root.player.clearQueue()
                    }
                }
            }
            ToolButton {
                text: "关闭"
                onClicked: root.close()
            }
        }
        ListView {
            id: queueList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.player.queueModel
            currentIndex: root.player.currentIndex
            onCurrentIndexChanged: if (!moving && currentIndex >= 0)
                                       positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: ItemDelegate {
                id: queueRow
                required property var trackData
                required property int index
                width: queueList.width
                implicitHeight: Math.max(64, contentItem.implicitHeight + 12)
                highlighted: queueRow.index === root.player.currentIndex
                onClicked: root.player.playQueueIndex(queueRow.index)
                contentItem: RowLayout {
                    spacing: 10
                    CoverImage {
                        entityName: queueRow.trackData.title
                        coverUrl: queueRow.trackData.coverUrl || ""
                        pixelSize: 100
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            text: queueRow.trackData.title
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            color: queueRow.highlighted ? Theme.accent : Theme.textPrimary
                        }
                        Label {
                            text: (queueRow.trackData.artist || "未知歌手") +
                                  (queueRow.highlighted ? " · 当前歌曲" : "")
                            color: queueRow.highlighted ? Theme.accent : Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    ToolButton {
                        text: "更多"
                        Accessible.name: queueRow.trackData.title + " 的队列操作"
                        onClicked: rowActions.open()
                        Menu {
                            id: rowActions
                            MenuItem {
                                text: "上移"
                                enabled: queueRow.index > 0
                                onTriggered: root.player.moveQueueItem(queueRow.index,
                                                                        queueRow.index - 1)
                            }
                            MenuItem {
                                text: "下移"
                                enabled: queueRow.index + 1 < root.player.queueCount
                                onTriggered: root.player.moveQueueItem(queueRow.index,
                                                                        queueRow.index + 1)
                            }
                            MenuItem {
                                text: "从队列移除"
                                onTriggered: root.player.removeQueueIndex(queueRow.index)
                            }
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: queueList.count === 0
                text: "播放队列为空"
                color: Theme.textSecondary
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Frame {
            visible: root.player.undoAvailable
            Layout.fillWidth: true
            Button {
                anchors.right: parent.right
                text: "撤销队列修改"
                onClicked: root.player.undoQueueEdit()
            }
        }
    }
}
