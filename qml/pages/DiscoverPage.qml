pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Page {
    id: root
    required property var discoverViewModel
    required property var playbackController
    property bool showHeading: true
    signal entryRequested(var entry)
    signal albumRequested(var track)
    signal addToPlaylistRequested(var track)

    Component.onCompleted: discoverViewModel.load()
    background: Rectangle { color: Theme.background }
    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: Math.min(1120, root.width - (root.width < 600 ? 32 : 64))
            x: Math.max(16, (root.width - width) / 2)
            spacing: 14
            Item { Layout.preferredHeight: 8 }
            Label {
                text: "发现音乐"
                visible: root.showHeading
                font.pixelSize: 28
                font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "每日推荐"
                    font.pixelSize: 22
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: "刷新"
                    enabled: !root.discoverViewModel.loading
                    onClicked: root.discoverViewModel.load()
                }
            }
            Label {
                visible: root.discoverViewModel.loading && root.discoverViewModel.dailyTracks.length === 0
                text: "正在加载推荐…"
                color: Theme.textSecondary
            }
            Label {
                visible: !root.discoverViewModel.loading && root.discoverViewModel.dailyTracks.length === 0
                text: "暂无每日推荐"
                color: Theme.textSecondary
            }
            Repeater {
                model: root.discoverViewModel.dailyTracks.slice(0, 10)
                SongRow {
                    required property var modelData
                    Layout.fillWidth: true
                    playbackController: root.playbackController
                    trackData: modelData
                    trackKey: modelData.key
                    title: modelData.title
                    artistText: modelData.artist
                    albumText: modelData.album
                    coverUrl: modelData.coverUrl
                    durationText: modelData.durationText
                    onAlbumRequested: track => root.albumRequested(track)
                    onAddToPlaylistRequested: track => root.addToPlaylistRequested(track)
                }
            }
            Label {
                text: "排行榜"
                font.pixelSize: 22
                font.bold: true
                Layout.topMargin: 16
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: root.discoverViewModel.ranks.slice(0, 12)
                    Button {
                        required property var modelData
                        text: modelData.title
                        onClicked: root.entryRequested(modelData)
                    }
                }
            }
            Label {
                text: "热门歌单"
                font.pixelSize: 22
                font.bold: true
                Layout.topMargin: 16
            }
            Repeater {
                model: root.discoverViewModel.playlists.slice(0, 12)
                ItemDelegate {
                    id: playlistEntry
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 76
                    onClicked: root.entryRequested(modelData)
                    contentItem: RowLayout {
                        spacing: 12
                        CoverImage {
                            Layout.preferredWidth: 56
                            Layout.preferredHeight: 56
                            coverUrl: playlistEntry.modelData.coverUrl
                            entityName: playlistEntry.modelData.title
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label {
                                text: playlistEntry.modelData.title
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: playlistEntry.modelData.subtitle || "歌单"
                                elide: Text.ElideRight
                                color: Theme.textSecondary
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
            Label {
                visible: root.discoverViewModel.errorMessage.length > 0
                text: root.discoverViewModel.errorMessage
                color: Theme.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { Layout.preferredHeight: 24 }
        }
    }
}
