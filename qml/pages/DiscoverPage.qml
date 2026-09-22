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
    readonly property var recommendationSource: ({ title: "每日推荐", hasMore: false })
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
            id: content
            width: Math.min(1120, root.width - (root.width < 600 ? 32 : 64))
            x: (root.width - width) / 2
            spacing: 24
            Item { Layout.preferredHeight: root.showHeading ? 8 : 0 }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: root.showHeading ? "发现音乐" : "每日推荐"
                    font.pixelSize: root.showHeading ? Theme.pageTitleSize : Theme.sectionTitleSize
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    visible: !root.showHeading
                    text: "播放全部"
                    highlighted: true
                    enabled: root.discoverViewModel.dailyTracks.length > 0
                    onClicked: root.playbackController.playCollection(root.discoverViewModel.dailyTracks, 0, root.recommendationSource)
                }
                ToolButton {
                    text: root.discoverViewModel.loading ? "更新中…" : "刷新"
                    enabled: !root.discoverViewModel.loading
                    onClicked: root.discoverViewModel.load(true)
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= 900 ? 2 : 1
                columnSpacing: 24
                rowSpacing: 24
                Frame {
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.width >= 900 ? content.width * 0.58 : content.width
                    Layout.alignment: Qt.AlignTop
                    padding: root.width < 600 ? 12 : 20
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            visible: root.showHeading
                            Layout.fillWidth: true
                            Label {
                                text: "每日推荐"
                                visible: root.showHeading
                                font.pixelSize: Theme.sectionTitleSize
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            Button {
                                text: "播放全部"
                                highlighted: true
                                enabled: root.discoverViewModel.dailyTracks.length > 0
                                onClicked: root.playbackController.playCollection(root.discoverViewModel.dailyTracks, 0, root.recommendationSource)
                            }
                        }
                        Repeater {
                            model: root.discoverViewModel.dailyTracks.slice(0, 5)
                            SongRow {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                playbackController: root.playbackController
                                artworkAlwaysVisible: true
                                trackData: modelData
                                trackKey: modelData.key
                                title: modelData.title
                                artistText: modelData.artist
                                albumText: modelData.album
                                coverUrl: modelData.coverUrl
                                durationText: modelData.durationText
                                onClicked: root.playbackController.playCollection(root.discoverViewModel.dailyTracks, index, root.recommendationSource)
                                onAlbumRequested: track => root.albumRequested(track)
                                onAddToPlaylistRequested: track => root.addToPlaylistRequested(track)
                            }
                        }
                        Label {
                            visible: root.discoverViewModel.dailyTracks.length === 0
                            text: root.discoverViewModel.loading ? "正在加载推荐…" : "暂时没有推荐歌曲"
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            Layout.topMargin: 24
                            Layout.bottomMargin: 24
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
                ColumnLayout {
                    id: playlistSection
                    visible: root.discoverViewModel.playlists.length > 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.width >= 900 ? content.width * 0.42 : content.width
                    Layout.alignment: Qt.AlignTop
                    spacing: 16
                    Label {
                        visible: playlistGrid.visible
                        text: "精选歌单"
                        font.pixelSize: Theme.sectionTitleSize
                        font.bold: true
                    }
                    GridLayout {
                        id: playlistGrid
                        visible: root.discoverViewModel.playlists.length > 0
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 24
                        Repeater {
                            model: root.discoverViewModel.playlists.slice(0, 4)
                            ItemDelegate {
                                id: playlistEntry
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: (playlistSection.width - 16) / 2
                                implicitHeight: contentItem.implicitHeight + 16
                                padding: 8
                                onClicked: root.entryRequested(modelData)
                                Accessible.name: modelData.title + "，打开歌单"
                                background: Rectangle {
                                    radius: Theme.radius
                                    color: playlistEntry.hovered || playlistEntry.down ? Theme.hoveredSurface : "transparent"
                                    border.width: playlistEntry.visualFocus ? 2 : 0
                                    border.color: Theme.accent
                                }
                                contentItem: ColumnLayout {
                                    spacing: 10
                                    CoverImage {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: width
                                        coverUrl: playlistEntry.modelData.coverUrl || ""
                                        entityName: playlistEntry.modelData.title
                                    }
                                    Label {
                                        text: playlistEntry.modelData.title
                                        font.bold: true
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 42
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Label {
                visible: root.discoverViewModel.errorMessage.length > 0 || root.discoverViewModel.refreshMessage.length > 0
                text: root.discoverViewModel.errorMessage || root.discoverViewModel.refreshMessage
                color: root.discoverViewModel.errorMessage ? Theme.error : Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: root.discoverViewModel.ranks.length > 0
                text: "排行榜"
                font.pixelSize: Theme.sectionTitleSize
                font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: root.discoverViewModel.ranks.slice(0, 4)
                    Button {
                        required property var modelData
                        text: modelData.title
                        onClicked: root.entryRequested(modelData)
                    }
                }
            }
            Item { Layout.preferredHeight: 8 }
        }
    }
}
