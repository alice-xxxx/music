pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Page {
    id: root
    required property var collection
    required property var player
    signal closeRequested
    signal addToPlaylistRequested(var track)
    signal commentsRequested(string kind, string id, string title)
    property bool descriptionExpanded: false

    function restorePosition() {
        songs.contentY = root.collection.scrollPosition;
    }
    function savePosition() {
        root.collection.saveScrollPosition(songs.contentY);
    }

    Component.onCompleted: Qt.callLater(() => songs.contentY = root.collection.scrollPosition)
    background: Rectangle { color: Theme.background }
    ListView {
        id: songs
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - (root.width < 600 ? 32 : 64), 1120)
        clip: true
        reuseItems: true
        spacing: 4
        model: root.collection.tracks
        onContentYChanged: if (flicking && contentY + height > contentHeight - 240)
                               root.collection.loadMore()
        header: ColumnLayout {
            width: songs.width
            spacing: 16
            Item { Layout.preferredHeight: root.width < 600 ? 1 : 8 }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width < 720 ? 1 : 2
                columnSpacing: 24
                rowSpacing: 16
                CoverImage {
                    Layout.preferredWidth: root.width < 720 ? 136 : 200
                    Layout.preferredHeight: Layout.preferredWidth
                    Layout.alignment: root.width < 720 ? Qt.AlignHCenter : Qt.AlignTop
                    coverUrl: root.collection.coverUrl
                    entityName: root.collection.title
                    imageKind: root.collection.kind === "artist" ? "头像" : "封面"
                    circular: root.collection.kind === "artist"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 8
                    Label {
                        text: root.collection.title
                        font.pixelSize: root.width < 600 ? 24 : 30
                        font.bold: true
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        horizontalAlignment: root.width < 720 ? Text.AlignHCenter : Text.AlignLeft
                    }
                    Label {
                        text: root.collection.tracks.length + (root.collection.hasMore ? "+ 首" : " 首")
                        color: Theme.textSecondary
                        Layout.alignment: root.width < 720 ? Qt.AlignHCenter : Qt.AlignLeft
                    }
                    Button {
                        text: root.collection.kind === "artist" ? "播放热门歌曲" :
                              root.collection.kind === "rank" ? "播放榜单" :
                              root.collection.kind === "playlist" ? "播放全部" : "播放专辑"
                        highlighted: true
                        enabled: root.collection.tracks.length > 0
                        Layout.alignment: root.width < 720 ? Qt.AlignHCenter : Qt.AlignLeft
                        onClicked: root.player.playCollection(root.collection.tracks, 0,
                                                              root.collection.source)
                    }
                    Button {
                        visible: root.collection.kind === "playlist"
                        text: root.collection.favoriteBusy ? "处理中…" :
                              root.collection.favorited ?
                              (root.collection.canUnfavorite ? "取消收藏" : "已在音乐库") : "收藏歌单"
                        enabled: !root.collection.favoriteBusy &&
                                 (!root.collection.favorited || root.collection.canUnfavorite)
                        Layout.alignment: root.width < 720 ? Qt.AlignHCenter : Qt.AlignLeft
                        onClicked: root.collection.toggleFavorite()
                    }
                    Button {
                        visible: root.collection.kind === "album" ||
                                 root.collection.kind === "playlist"
                        text: "评论"
                        onClicked: {
                            const source = root.collection.source;
                            root.commentsRequested(root.collection.kind,
                                                   root.collection.kind === "playlist" ?
                                                       source.globalId : source.id,
                                                   root.collection.title);
                        }
                    }
                }
            }
            Label {
                text: root.collection.description
                visible: text.length > 0
                maximumLineCount: root.descriptionExpanded ? 1000 : 2
                elide: root.descriptionExpanded ? Text.ElideNone : Text.ElideRight
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Theme.textSecondary
            }
            ToolButton {
                visible: root.collection.description.length > 100
                text: root.descriptionExpanded ? "收起简介" : "展开简介"
                onClicked: root.descriptionExpanded = !root.descriptionExpanded
            }
            Label {
                text: root.collection.favoriteMessage
                visible: text.length > 0
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                text: root.collection.errorMessage
                visible: text.length > 0
                color: Theme.error
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Button {
                visible: root.collection.errorMessage.length > 0
                text: "重试加载"
                onClicked: root.collection.retry()
            }
            RowLayout {
                visible: root.collection.loading || root.collection.detailLoading
                BusyIndicator {
                    running: parent.visible
                    implicitHeight: 28
                    implicitWidth: 28
                }
                Label {
                    text: root.collection.tracks.length > 0 ? "正在更新…" : "正在加载内容…"
                    color: Theme.textSecondary
                }
            }
            Label {
                text: root.collection.kind === "artist" ? "热门歌曲" : "歌曲"
                visible: root.collection.tracks.length > 0
                font.pixelSize: 20
                font.bold: true
                Layout.topMargin: 4
            }
            Item { Layout.preferredHeight: 4 }
        }
        delegate: SongRow {
            id: song
            required property var modelData
            required property int index
            width: songs.width
            playbackController: root.player
            trackData: modelData
            trackKey: modelData.key
            title: modelData.title
            artistText: modelData.artist
            albumText: modelData.album
            coverUrl: modelData.coverUrl
            durationText: modelData.durationText
            onClicked: root.player.playCollection(root.collection.tracks, song.index,
                                                  root.collection.source)
            onAddToPlaylistRequested: track => root.addToPlaylistRequested(track)
            onAlbumRequested: track => root.collection.openNestedAlbum(track, songs.contentY)
        }
        footer: ColumnLayout {
            width: songs.width
            spacing: 12
            Button {
                visible: root.collection.hasMore
                text: root.collection.loading ? "正在加载…" : "加载更多"
                enabled: !root.collection.loading
                onClicked: root.collection.loadMore()
            }
            Label {
                visible: root.collection.tracks.length === 0 && !root.collection.loading &&
                         root.collection.errorMessage.length === 0
                text: root.collection.kind === "artist" ? "暂时没有可播放的热门歌曲" :
                                                      "这里还没有歌曲"
                color: Theme.textSecondary
            }
            Item { Layout.preferredHeight: 24 }
        }
        ScrollBar.vertical: ScrollBar {}
    }
}
