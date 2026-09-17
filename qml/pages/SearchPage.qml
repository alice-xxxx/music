pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Page {
    id: root
    required property var searchViewModel
    required property var playbackController
    signal loginRequested
    signal albumRequested(var track)
    signal entryRequested(var entry)
    signal addToPlaylistRequested(var track)
    property bool waiting: false
    property bool showHeading: true
    property bool longWait: false
    property real entryScroll: 0
    property bool restoreEntryScroll: false
    background: Rectangle {
        color: Theme.background
    }
    padding: width < 600 ? 16 : 32
    Timer {
        id: debounce
        interval: 300
        onTriggered: root.searchViewModel.submitSearch()
    }
    Timer {
        id: waitDelay
        interval: 200
        onTriggered: root.waiting = true
    }
    Timer {
        id: longDelay
        interval: 3000
        onTriggered: root.longWait = true
    }
    Connections {
        target: root.searchViewModel
        function onEntriesAboutToChange(append) {
            root.entryScroll = append ? entryResults.contentY : 0;
            root.restoreEntryScroll = true;
        }
        function onStatusChanged() {
            if (root.restoreEntryScroll) {
                root.restoreEntryScroll = false;
                Qt.callLater(() => entryResults.contentY = root.entryScroll);
            }
            if (root.searchViewModel.status === 1) {
                waitDelay.restart();
                longDelay.restart();
            } else {
                waitDelay.stop();
                longDelay.stop();
                root.waiting = false;
                root.longWait = false;
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label {
            text: "搜索音乐"
            visible: root.showHeading
            font.pixelSize: 28
            font.bold: true
            color: Theme.textPrimary
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: input.implicitHeight
            TextField {
                id: input
                anchors.left: parent.left
                anchors.right: parent.right
                placeholderText: "输入关键词搜索音乐"
                rightPadding: clearSearch.visible ? 52 : 12
                text: root.searchViewModel.query
                onTextEdited: {
                    root.searchViewModel.query = text;
                    debounce.stop();
                    if (!inputMethodComposing)
                        debounce.restart();
                }
                onInputMethodComposingChanged: if (!inputMethodComposing)
                                                   debounce.restart()
                onAccepted: {
                    debounce.stop();
                    root.searchViewModel.submitSearch();
                }
                Accessible.name: "搜索关键词"
            }
            ToolButton {
                id: clearSearch
                anchors.right: input.right
                anchors.verticalCenter: input.verticalCenter
                visible: input.text.length > 0
                text: "清除"
                Accessible.name: "清除搜索关键词"
                onClicked: {
                    debounce.stop();
                    root.searchViewModel.query = "";
                    root.searchViewModel.submitSearch();
                    input.forceActiveFocus();
                }
            }
        }
        TabBar {
            Layout.fillWidth: true
            currentIndex: ["song", "album", "artist", "playlist"].indexOf(
                root.searchViewModel.category)
            onCurrentIndexChanged: {
                if (currentIndex < 0)
                    return;
                debounce.stop();
                root.searchViewModel.category = ["song", "album", "artist",
                                                 "playlist"][currentIndex];
            }
            NavigationTab {
                text: "歌曲"
            }
            NavigationTab {
                text: "专辑"
            }
            NavigationTab {
                text: "歌手"
            }
            NavigationTab {
                text: "歌单"
            }
        }
        RowLayout {
            visible: root.searchViewModel.status === 0
            Layout.fillWidth: true
            Label {
                text: root.searchViewModel.recentQueries.length ? "最近搜索" :
                                                                  "输入歌曲、歌手、专辑或歌单开始搜索"
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
            ToolButton {
                visible: root.searchViewModel.recentQueries.length > 0
                text: "清理"
                Accessible.name: "清理最近搜索"
                onClicked: root.searchViewModel.clearRecentQueries()
            }
        }
        Flow {
            visible: root.searchViewModel.status === 0
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.searchViewModel.recentQueries
                Button {
                    required property string modelData
                    text: modelData
                    onClicked: {
                        root.searchViewModel.query = modelData;
                        root.searchViewModel.submitSearch();
                    }
                }
            }
        }
        RowLayout {
            visible: root.waiting
            BusyIndicator {
                running: root.waiting
                implicitWidth: 28
                implicitHeight: 28
            }
            Label {
                text: root.longWait ? "仍在搜索，可以修改关键词或清空取消" : "正在搜索…"
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        Label {
            visible: root.searchViewModel.resultsQuery.length > 0 && (
                         root.searchViewModel.resultsQuery !== root.searchViewModel.query.trim()
                         || root.searchViewModel.resultsCategory !== root.searchViewModel.category)
            text: "当前显示：" + root.searchViewModel.resultsQuery + " · " + ({
                                                                             song: "歌曲",
                                                                             album: "专辑",
                                                                             artist: "歌手",
                                                                             playlist: "歌单"
                                                                         })[root.searchViewModel.resultsCategory]
            color: Theme.textSecondary
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: root.searchViewModel.status === 4
            Layout.fillWidth: true
            Label {
                text: root.searchViewModel.authRequired ? "此服务搜索需要登录，关键词已保留" :
                                                          root.searchViewModel.errorMessage
                color: Theme.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                text: root.searchViewModel.authRequired ? "登录后继续搜索" : "重试"
                onClicked: root.searchViewModel.authRequired ? root.loginRequested() :
                                                               root.searchViewModel.retry()
            }
        }
        Label {
            visible: root.searchViewModel.status === 3
            text: "没有找到结果，换个关键词试试。"
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ListView {
            id: results
            visible: root.searchViewModel.resultsCategory === "song"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            spacing: 4
            model: root.searchViewModel
            onContentYChanged: if (flicking && contentY + height > contentHeight - 240)
                                   root.searchViewModel.loadMore()
            delegate: SongRow {
                width: results.width
                playbackController: root.playbackController
                onAlbumRequested: track => root.albumRequested(track)
                onAddToPlaylistRequested: track => root.addToPlaylistRequested(track)
            }
            ScrollBar.vertical: ScrollBar {}
            footer: Column {
                width: results.width
                Label {
                    text: root.searchViewModel.pageError
                    visible: text.length > 0
                    color: Theme.error
                    width: parent.width
                    wrapMode: Text.Wrap
                }
                Button {
                    visible: root.searchViewModel.hasMore
                    text: root.searchViewModel.loadingMore ? "正在加载…" :
                                                             root.searchViewModel.pageError.length
                                                             ? "重试加载更多" : "加载更多"
                    enabled: !root.searchViewModel.loadingMore
                    onClicked: root.searchViewModel.loadMore()
                }
            }
        }
        ListView {
            id: entryResults
            visible: root.searchViewModel.resultsCategory !== "song"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.searchViewModel.entries
            onContentYChanged: if (flicking && contentY + height > contentHeight - 240)
                                   root.searchViewModel.loadMore()
            delegate: ItemDelegate {
                id: entryRow
                required property var modelData
                required property int index
                width: entryResults.width
                implicitHeight: Math.max(64, contentItem.implicitHeight + 16)
                onClicked: root.entryRequested(entryRow.modelData)
                contentItem: RowLayout {
                    spacing: 12
                    CoverImage {
                        coverUrl: entryRow.modelData.coverUrl
                        entityName: entryRow.modelData.title
                        imageKind: entryRow.modelData.kind === "artist" ? "头像" : "封面"
                        circular: entryRow.modelData.kind === "artist"
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: entryRow.modelData.title
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            font.bold: true
                        }
                        Label {
                            text: entryRow.modelData.subtitle
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            color: Theme.textSecondary
                        }
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
            footer: Column {
                width: entryResults.width
                Label {
                    text: root.searchViewModel.pageError
                    visible: text.length > 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.error
                }
                Button {
                    visible: root.searchViewModel.hasMore
                    text: root.searchViewModel.loadingMore ? "正在加载…" : "加载更多"
                    enabled: !root.searchViewModel.loadingMore
                    onClicked: root.searchViewModel.loadMore()
                }
            }
        }
    }
}
