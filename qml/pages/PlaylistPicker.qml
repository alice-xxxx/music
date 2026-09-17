pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../theme"

Dialog {
    id: root
    required property var library
    property var track: ({})
    title: "加入歌单"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(440, Overlay.overlay.width - 32)
    height: Math.min(560, Overlay.overlay.height - 32)
    standardButtons: Dialog.Close
    onOpened: root.library.load()

    Connections {
        target: root.library
        function onActionChanged() {
            if (root.library.actionSucceeded && root.library.actionKind === "add")
                closeAfterSuccess.restart();
            if (root.library.actionSucceeded && root.library.actionKind === "create")
                createDialog.close();
        }
    }
    Timer {
        id: closeAfterSuccess
        interval: 700
        onTriggered: root.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            CoverImage {
                coverUrl: root.track.coverUrl || ""
                entityName: root.track.title || "当前歌曲"
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label {
                    text: root.track.title || ""
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: root.track.artist || "未知歌手"
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
        Label {
            text: root.library.actionMessage
            visible: text.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: root.library.actionUncertain ? Theme.error : Theme.textSecondary
        }
        Label {
            text: root.library.errorMessage
            visible: text.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: Theme.error
        }
        RowLayout {
            visible: root.library.loading
            BusyIndicator {
                running: parent.visible
                implicitWidth: 28
                implicitHeight: 28
            }
            Label {
                text: root.library.playlists.length > 0 ? "正在更新歌单…" : "正在加载歌单…"
                color: Theme.textSecondary
            }
        }
        Button {
            visible: root.library.errorMessage.length > 0
            text: "重试加载"
            onClicked: root.library.retry()
        }
        Button {
            visible: root.library.actionUncertain
            text: "查询操作结果"
            enabled: !root.library.actionBusy
            onClicked: root.library.confirmAction()
        }
        ListView {
            id: playlists
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.library.playlists
            clip: true
            delegate: ItemDelegate {
                id: entry
                required property var modelData
                required property int index
                width: playlists.width
                implicitHeight: 64
                enabled: !root.library.actionBusy && !root.library.actionUncertain
                onClicked: root.library.addTrack(entry.index, root.track)
                contentItem: RowLayout {
                    CoverImage {
                        coverUrl: entry.modelData.cover || ""
                        entityName: entry.modelData.title
                        pixelSize: 100
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: entry.modelData.title
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            text: (entry.modelData.count || 0) + " 首"
                            color: Theme.textSecondary
                        }
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                visible: playlists.count === 0 && !root.library.loading &&
                         root.library.errorMessage.length === 0
                Label {
                    text: "还没有可选的歌单"
                    color: Theme.textSecondary
                }
                Button {
                    text: "新建歌单"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: createDialog.open()
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Button {
            visible: root.library.hasMore
            text: root.library.loading ? "正在加载…" : "加载更多歌单"
            enabled: !root.library.loading
            onClicked: root.library.loadMorePlaylists()
        }
    }

    Dialog {
        id: createDialog
        title: "新建歌单"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, root.width - 32)
        standardButtons: Dialog.Cancel
        onOpened: playlistName.forceActiveFocus()
        contentItem: ColumnLayout {
            Label { text: "歌单名称" }
            TextField {
                id: playlistName
                Layout.fillWidth: true
                maximumLength: 100
                placeholderText: "输入歌单名称"
                onAccepted: if (createButton.enabled) createButton.clicked()
            }
            Label {
                text: playlistName.text.length + " / 100"
                color: Theme.textSecondary
                Layout.alignment: Qt.AlignRight
            }
            Button {
                id: createButton
                text: root.library.actionBusy ? "正在创建…" : "创建"
                highlighted: true
                enabled: playlistName.text.trim().length > 0 && !root.library.actionBusy &&
                         !root.library.actionUncertain
                onClicked: root.library.createPlaylist(playlistName.text)
            }
        }
    }
}
