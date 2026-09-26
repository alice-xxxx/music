pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Page {
    id: root
    required property var favorites
    required property var libraryViewModel
    required property var sessionManager
    required property var playbackController
    signal loginRequested
    signal albumRequested(var track)
    signal addToPlaylistRequested(var track)
    readonly property int playlistsSection: 0
    readonly property int recentSection: 1
    readonly property int favoritesSection: 2
    property int section: playlistsSection
    property bool active: false
    property bool showHeading: true
    readonly property bool canGoBack: detail || section !== playlistsSection
    readonly property string pageTitle: detail ? libraryViewModel.title :
                                        section === recentSection ? "最近播放" :
                                        section === favoritesSection ? "喜欢的音乐" : "音乐库"
    readonly property bool modalOpen: createDialog.opened || editDialog.opened ||
                                      deleteDialog.opened || clearHistoryDialog.opened
    function navigateBack() {
        if (detail) {
            libraryViewModel.closePlaylist();
            Qt.callLater(() => playlists.contentY = playlistScrollY);
        } else {
            section = playlistsSection;
        }
    }
    property bool loaded: false
    property real playlistScrollY: 0
    readonly property bool detail: section === playlistsSection && libraryViewModel.title.length > 0
    readonly property var displayedTracks: detail ? libraryViewModel.tracks :
                                            section === favoritesSection ? favorites.tracks : playbackController.recentTracks
    readonly property var playbackSource: detail ? libraryViewModel.source : ({ title: pageTitle, hasMore: false })
    readonly property bool songSection: detail || section !== playlistsSection

    onSectionChanged: if (section === favoritesSection && sessionManager.authenticated)
                          favorites.refresh()
    onActiveChanged: if (active && !loaded && sessionManager.authenticated) {
                         loaded = true;
                         libraryViewModel.load();
                     }
    Connections {
        target: root.sessionManager
        function onAuthenticatedChanged() {
            root.loaded = false;
            if (root.active && root.sessionManager.authenticated) {
                root.loaded = true;
                root.libraryViewModel.load();
                root.favorites.refresh();
            }
        }
    }
    Connections {
        target: root.libraryViewModel
        function onActionChanged() {
            if (root.libraryViewModel.actionSucceeded &&
                    root.libraryViewModel.actionKind === "create") {
                createDialog.close();
                playlistName.clear();
            }
            if (root.libraryViewModel.actionSucceeded &&
                    root.libraryViewModel.actionKind === "delete")
                deleteDialog.close();
            if (root.libraryViewModel.actionSucceeded &&
                    root.libraryViewModel.actionKind === "update")
                editDialog.close();
        }
    }

    background: Rectangle { color: Theme.background }
    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: root.showHeading ? 0 : 16
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - (root.width < 600 ? 32 : 64), 1120)
        spacing: 12
        Label {
            text: root.detail ? root.libraryViewModel.title :
                  root.section === root.recentSection ? "最近播放" :
                  root.section === root.favoritesSection ? "喜欢的音乐" : "音乐库"
            visible: root.showHeading
            font.pixelSize: root.detail ? 24 : Theme.pageTitleSize
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.topMargin: 20
        }
        RowLayout {
            visible: !root.detail && root.section === root.playlistsSection
            Layout.fillWidth: true
            Button {
                text: "喜欢的音乐" + (root.sessionManager.authenticated ? " · " + root.favorites.tracks.length : "")
                implicitHeight: 80
                font.bold: true
                Layout.fillWidth: true
                onClicked: root.sessionManager.authenticated ? root.section = root.favoritesSection :
                                                                 root.loginRequested()
            }
            Button {
                text: "最近播放 · " + root.playbackController.recentTracks.length
                implicitHeight: 80
                font.bold: true
                Layout.fillWidth: true
                onClicked: root.section = root.recentSection
            }
        }
        RowLayout {
            visible: root.section !== root.playlistsSection || root.detail
            Layout.fillWidth: true
            Button {
                text: "播放全部"
                highlighted: true
                visible: !root.detail && root.songSection
                enabled: root.displayedTracks.length > 0
                onClicked: root.playbackController.playCollection(root.displayedTracks, 0, root.playbackSource)
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                id: historyButton
                property var actionMenu: historyMenu
                visible: root.section === root.recentSection
                text: "管理"
                onClicked: historyMenu.openAt(historyButton)
                ActionMenu {
                    id: historyMenu
                    objectName: "historyActionMenu"
                    ActionMenuItem {
                        text: "清除最近播放"
                        destructive: true
                        enabled: root.playbackController.recentTracks.length > 0
                        onTriggered: clearHistoryDialog.open()
                    }
                }
            }
            ToolButton {
                visible: root.section === root.favoritesSection
                text: root.favorites.busy ? "正在刷新…" : "刷新"
                enabled: !root.favorites.busy
                onClicked: root.favorites.refresh()
            }
        }
        Frame {
            visible: root.detail
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 16
                CoverImage {
                    coverUrl: root.libraryViewModel.coverUrl
                    entityName: root.libraryViewModel.title
                    Layout.preferredWidth: root.width < 600 ? 88 : 128
                    Layout.preferredHeight: Layout.preferredWidth
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        text: root.libraryViewModel.tracks.length +
                              (root.libraryViewModel.hasMore ? "+ 首" : " 首")
                        color: Theme.textSecondary
                    }
                    Button {
                        text: "播放全部"
                        highlighted: true
                        enabled: root.libraryViewModel.tracks.length > 0
                        onClicked: root.playbackController.playCollection(
                                       root.libraryViewModel.tracks, 0,
                                       root.libraryViewModel.source)
                    }
                }
                ToolButton {
                    id: detailButton
                    property var actionMenu: detailMenu
                    text: "管理"
                    onClicked: detailMenu.openAt(detailButton)
                    ActionMenu {
                        id: detailMenu
                        objectName: "detailActionMenu"
                        ActionMenuItem {
                            text: "编辑歌单"
                            enabled: !root.libraryViewModel.actionBusy &&
                                     !root.libraryViewModel.actionUncertain
                            onTriggered: editDialog.open()
                        }
                        ActionMenuItem {
                            text: "删除歌单"
                            destructive: true
                            enabled: !root.libraryViewModel.actionBusy &&
                                     !root.libraryViewModel.actionUncertain
                            onTriggered: deleteDialog.open()
                        }
                    }
                }
            }
        }
        RowLayout {
            visible: !root.detail && root.section === root.playlistsSection
            Layout.fillWidth: true
            Label {
                text: "我的歌单"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }
            ToolButton {
                text: "刷新"
                visible: root.sessionManager.authenticated
                enabled: !root.libraryViewModel.loading
                onClicked: root.libraryViewModel.load()
            }
            Button {
                text: "+ 新建"
                visible: root.sessionManager.authenticated
                onClicked: createDialog.open()
            }
        }
        Frame {
            visible: root.section === root.playlistsSection && !root.detail &&
                     !root.sessionManager.authenticated
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    text: "登录后同步你的歌单与收藏"
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    text: "登录"
                    highlighted: true
                    onClicked: root.loginRequested()
                }
            }
        }
        Label {
            visible: root.section === root.favoritesSection && root.favorites.message.length > 0
            text: root.favorites.message
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            color: root.favorites.uncertain ? Theme.error : Theme.textSecondary
        }
        Label {
            visible: root.section === root.playlistsSection &&
                     root.libraryViewModel.errorMessage.length > 0
            text: root.libraryViewModel.errorMessage
            color: Theme.error
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Button {
            visible: root.section === root.playlistsSection &&
                     root.libraryViewModel.errorMessage.length > 0
            text: "重试加载"
            onClicked: root.libraryViewModel.retry()
        }
        Button {
            visible: root.libraryViewModel.actionUncertain
            text: "查询操作结果"
            enabled: !root.libraryViewModel.actionBusy
            onClicked: root.libraryViewModel.confirmAction()
        }
        RowLayout {
            visible: root.section === root.playlistsSection && root.libraryViewModel.loading
            BusyIndicator {
                running: parent.visible
                implicitHeight: 28
                implicitWidth: 28
            }
            Label {
                text: root.libraryViewModel.playlists.length > 0 ? "正在更新歌单…" :
                                                                  "正在加载歌单…"
                color: Theme.textSecondary
            }
        }
        ListView {
            id: playlists
            visible: root.section === root.playlistsSection && !root.detail &&
                     root.sessionManager.authenticated
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.libraryViewModel.playlists
            onContentYChanged: if (flicking && contentY + height > contentHeight - 240)
                                   root.libraryViewModel.loadMorePlaylists()
            delegate: ItemDelegate {
                id: playlistRow
                required property var modelData
                required property int index
                width: playlists.width
                implicitHeight: 68
                onClicked: {
                    root.playlistScrollY = playlists.contentY;
                    root.libraryViewModel.openPlaylist(playlistRow.index);
                }
                contentItem: RowLayout {
                    CoverImage {
                        coverUrl: playlistRow.modelData.cover || ""
                        entityName: playlistRow.modelData.title
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: playlistRow.modelData.title
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            text: (playlistRow.modelData.count || 0) + " 首"
                            color: Theme.textSecondary
                        }
                    }
                }
            }
            footer: Button {
                visible: root.libraryViewModel.hasMore
                text: root.libraryViewModel.loading ? "正在加载…" : "加载更多"
                enabled: !root.libraryViewModel.loading
                onClicked: root.libraryViewModel.loadMorePlaylists()
            }
            ScrollBar.vertical: ScrollBar {}
        }
        ListView {
            id: songs
            visible: root.songSection
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.displayedTracks
            onContentYChanged: if (root.detail && flicking && contentY + height >
                                   contentHeight - 240) root.libraryViewModel.loadMore()
            delegate: SongRow {
                id: songRow
                required property var modelData
                required property int index
                width: songs.width
                playbackController: root.playbackController
                trackData: modelData
                trackKey: modelData.key
                title: modelData.title
                artistText: modelData.artist || "未知歌手"
                durationText: modelData.durationText || ""
                coverUrl: modelData.coverUrl || ""
                albumText: modelData.album || ""
                removable: root.detail
                onClicked: root.playbackController.playCollection(root.displayedTracks, songRow.index, root.playbackSource)
                onAlbumRequested: track => root.albumRequested(track)
                onAddToPlaylistRequested: track => root.addToPlaylistRequested(track)
                onRemoveRequested: root.libraryViewModel.removeTrack(songRow.index)
            }
            footer: Button {
                visible: root.detail && root.libraryViewModel.hasMore
                text: root.libraryViewModel.loading ? "正在加载…" : "加载更多"
                enabled: !root.libraryViewModel.loading
                onClicked: root.libraryViewModel.loadMore()
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Label {
            visible: root.songSection && songs.count === 0 &&
                     !(root.section === root.favoritesSection && root.favorites.busy) &&
                     !(root.detail && root.libraryViewModel.loading)
            text: root.section === root.recentSection ? "还没有最近播放" :
                  root.section === root.favoritesSection ?
                                      (root.sessionManager.authenticated ? "还没有喜欢的音乐" :
                                                                          "登录后查看喜欢的音乐") :
                                      "这个歌单还没有歌曲"
            color: Theme.textSecondary
        }
        Label {
            visible: root.section === root.playlistsSection && !root.detail &&
                     root.sessionManager.authenticated &&
                     playlists.count === 0 && !root.libraryViewModel.loading &&
                     root.libraryViewModel.errorMessage.length === 0
            text: "还没有歌单，创建一个开始整理音乐"
            color: Theme.textSecondary
        }
        Item {
            visible: !root.songSection && !root.sessionManager.authenticated
            Layout.fillHeight: true
        }
        Item { Layout.preferredHeight: 8 }
    }

    Dialog {
        id: createDialog
        title: "新建歌单"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(400, root.width - 32)
        standardButtons: Dialog.Cancel
        onOpened: playlistName.forceActiveFocus()
        contentItem: ColumnLayout {
            Label { text: "歌单名称" }
            TextField {
                id: playlistName
                placeholderText: "输入歌单名称"
                Layout.fillWidth: true
                onAccepted: if (createButton.enabled) createButton.clicked()
            }
            Label {
                text: root.libraryViewModel.actionMessage
                visible: text.length > 0 && !root.libraryViewModel.actionSucceeded
                wrapMode: Text.Wrap
                color: Theme.error
                Layout.fillWidth: true
            }
            Button {
                id: createButton
                text: root.libraryViewModel.actionBusy ? "正在创建…" : "创建"
                highlighted: true
                enabled: !root.libraryViewModel.actionBusy &&
                         !root.libraryViewModel.actionUncertain
                onClicked: root.libraryViewModel.createPlaylist(playlistName.text)
            }
        }
    }
    Dialog {
        id: editDialog
        objectName: "editPlaylistDialog"
        title: "编辑歌单"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(440, root.width - 32)
        standardButtons: Dialog.Cancel
        onOpened: {
            editPlaylistName.text = root.libraryViewModel.title;
            editPlaylistDescription.text = root.libraryViewModel.description;
            editPlaylistName.forceActiveFocus();
        }
        contentItem: ColumnLayout {
            Label { text: "歌单名称" }
            TextField {
                id: editPlaylistName
                Layout.fillWidth: true
            }
            Label { text: "简介" }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                TextArea {
                    id: editPlaylistDescription
                    wrapMode: TextEdit.Wrap
                    placeholderText: "填写歌单简介（可选）"
                }
            }
            Label {
                text: root.libraryViewModel.actionMessage
                visible: text.length > 0 && !root.libraryViewModel.actionSucceeded
                wrapMode: Text.Wrap
                color: Theme.error
                Layout.fillWidth: true
            }
            Button {
                text: root.libraryViewModel.actionBusy ? "正在保存…" : "保存"
                highlighted: true
                enabled: !root.libraryViewModel.actionBusy &&
                         !root.libraryViewModel.actionUncertain
                onClicked: root.libraryViewModel.updateSelectedPlaylist(
                               editPlaylistName.text, editPlaylistDescription.text)
            }
        }
    }
    Dialog {
        id: deleteDialog
        objectName: "deletePlaylistDialog"
        title: "删除歌单"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(400, root.width - 32)
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            Label {
                text: "删除“" + root.libraryViewModel.title +
                      "”？歌曲仍会保留在其他歌单中。"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "取消"; onClicked: deleteDialog.close() }
                Button {
                    text: root.libraryViewModel.actionBusy ? "正在删除…" : "删除歌单"
                    enabled: !root.libraryViewModel.actionBusy &&
                             !root.libraryViewModel.actionUncertain
                    onClicked: root.libraryViewModel.deleteSelectedPlaylist()
                }
            }
        }
    }
    Dialog {
        id: clearHistoryDialog
        objectName: "clearHistoryDialog"
        title: "清除最近播放"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(400, root.width - 32)
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            Label {
                text: "清除本机保存的最近播放记录？当前队列和正在播放的歌曲会保留。"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "取消"; onClicked: clearHistoryDialog.close() }
                Button {
                    text: "清除记录"
                    onClicked: {
                        root.playbackController.clearHistory();
                        clearHistoryDialog.close();
                    }
                }
            }
        }
    }
}
