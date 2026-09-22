pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import "../theme"
import "../components"

Page {
    id: root
    required property var favorites
    required property var player
    signal queueRequested
    signal albumRequested(var track)
    signal artistRequested(var artist)
    property bool showLyrics: false
    property bool browsingLyrics: false
    function timeText(ms) {
        const seconds = Math.floor(Math.max(0, ms) / 1000);
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toString().padStart(2, "0");
    }
    function followLyrics() {
        if (!browsingLyrics && player.lyricLines.currentIndex >= 0)
            lyricList.positionViewAtIndex(player.lyricLines.currentIndex, ListView.Center);
    }
    background: Rectangle {
        color: Theme.background
    }
    Connections {
        target: root.player.lyricLines
        function onCurrentIndexChanged() {
            root.followLyrics();
        }
        function onModelReset() {
            root.browsingLyrics = false;
            Qt.callLater(root.followLyrics);
        }
    }
    readonly property bool wide: width >= 960
    RowLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - (root.width < 600 ? 32 : 64), root.wide ? 1120 : 480)
        height: Math.max(0, parent.height - 32)
        spacing: root.wide ? 64 : 0
        ColumnLayout {
            id: controls
            Layout.preferredWidth: root.wide ? 400 : 0
            Layout.fillWidth: !root.wide
            Layout.fillHeight: true
            spacing: 6
            Label {
                objectName: "playingTitle"
                text: root.player.title || "尚未播放"
                font.pixelSize: root.font.pixelSize * 1.65
                font.bold: true
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Layout.fillWidth: true
                horizontalAlignment: root.wide ? Text.AlignLeft : Text.AlignHCenter
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Button {
                    id: artistLink
                    text: root.player.artist || "歌手信息暂缺"
                    flat: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    enabled: (root.player.currentTrack.artists || []).length > 0
                    onClicked: artistMenu.open()
                    Menu {
                        id: artistMenu
                        Instantiator {
                            model: root.player.currentTrack.artists || []
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.name
                                onTriggered: root.artistRequested(modelData)
                            }
                            onObjectAdded: (index, object) => artistMenu.insertItem(index,
                                                                                    object)
                            onObjectRemoved: (index, object) => artistMenu.removeItem(object)
                        }
                    }
                }
                Label {
                    text: "·"
                    color: Theme.textSecondary
                }
                Button {
                    id: albumLink
                    text: root.player.currentTrack.album || "专辑信息暂缺"
                    flat: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    enabled: !!root.player.currentTrack.albumId
                    onClicked: root.albumRequested(root.player.currentTrack)
                }
            }
            Item {
                id: primaryContent
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 100
                CoverImage {
                    objectName: "playingArtwork"
                    visible: root.wide || !root.showLyrics
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height, root.wide ? 400 : 360)
                    height: width
                    entityName: root.player.title
                    coverUrl: root.player.currentTrack.coverUrl || ""
                }
            }
            Label {
                text: root.player.errorMessage
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: Theme.error
            }
            Label {
                text: root.player.playbackStatus
                visible: text.length > 0
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.textSecondary
            }
            Label {
                text: root.favorites.message
                visible: text.length > 0
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.textSecondary
            }
            Slider {
                id: progress
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.player.duration)
                enabled: root.player.seekable
                Binding {
                    target: progress
                    property: "value"
                    value: root.player.position
                    when: !progress.pressed
                    restoreMode: Binding.RestoreNone
                }
                onPressedChanged: if (!pressed)
                                      root.player.seek(value)
                onMoved: if (!pressed) root.player.seek(value)
                Accessible.name: "播放进度"
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: root.timeText(progress.pressed ? progress.value : root.player.position)
                    color: Theme.textSecondary
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.player.audioBitRate > 0 ? Math.round(root.player.audioBitRate / 1000)
                                                         + " kbps" : ""
                    color: Theme.textSecondary
                }
                Label {
                    text: root.player.duration > 0 ? root.timeText(root.player.duration) : "—"
                    color: Theme.textSecondary
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 28
                IconButton {
                    symbol: "previous"
                    text: "上一首"
                    enabled: root.player.queueCount > 0
                    onClicked: root.player.previous()
                }
                IconButton {
                    symbol: root.player.errorMessage ? "retry" : root.player.desiredPlaying
                                                       ? "pause" : "play"
                    text: root.player.errorMessage ? "重试播放" : root.player.desiredPlaying ? "暂停" :
                                                                                           "播放"
                    primary: true
                    busy: root.player.preparing
                    enabled: root.player.hasCurrentTrack
                    onClicked: root.player.togglePlayback()
                }
                IconButton {
                    symbol: "next"
                    text: "下一首"
                    enabled: root.player.queueCount > 0
                    onClicked: root.player.next()
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: root.width < 400 ? 4 : 12
                IconButton {
                    symbol: "heart"
                    text: root.favorites.uncertain ? "查询喜欢状态" : root.favorites.liked ? "取消喜欢" : "喜欢"
                    checked: root.favorites.liked
                    busy: root.favorites.busy
                    enabled: !root.favorites.busy && root.player.hasCurrentTrack
                    onClicked: root.favorites.toggle()
                }

                IconButton {
                    symbol: "shuffle"
                    text: root.player.shuffle ? "关闭随机播放" : "开启随机播放"
                    checked: root.player.shuffle
                    onClicked: root.player.shuffle = !root.player.shuffle
                }
                IconButton {
                    symbol: root.player.repeatMode === 2 ? "repeat-one" : "repeat"
                    text: ["顺序播放", "列表循环", "单曲循环"][root.player.repeatMode] + "，切换模式"
                    checked: root.player.repeatMode !== 0
                    onClicked: root.player.repeatMode = (root.player.repeatMode + 1) % 3
                }
                IconButton {
                    symbol: "lyrics"
                    text: root.showLyrics ? "显示封面" : "显示歌词"
                    visible: !root.wide
                    checked: root.showLyrics
                    onClicked: {
                        root.showLyrics = !root.showLyrics;
                        Qt.callLater(root.followLyrics);
                    }
                }
                IconButton {
                    symbol: "queue"
                    text: "播放队列，" + root.player.queueCount + "首"
                    onClicked: root.queueRequested()
                }
            }
        }
        Item {
            id: desktopLyrics
            visible: root.wide
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
    ColumnLayout {
        parent: root.wide ? desktopLyrics : primaryContent
        anchors.fill: parent
        visible: root.wide || root.showLyrics
        ListView {
            id: lyricList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.player.lyricLines
            spacing: 24
            topMargin: height / 3
            bottomMargin: height / 3
            onMovementStarted: root.browsingLyrics = true
            onHeightChanged: Qt.callLater(root.followLyrics)
            ScrollBar.vertical: ScrollBar {}
            delegate: Label {
                id: lyricRow
                required property string lineText
                required property int index
                width: lyricList.width
                text: lyricRow.lineText || " "
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                horizontalAlignment: root.wide ? Text.AlignLeft : Text.AlignHCenter
                font.pixelSize: root.font.pixelSize * (root.wide ? 2 : 1.5)
                font.bold: lyricRow.index === root.player.lyricLines.currentIndex
                color: lyricRow.index === root.player.lyricLines.currentIndex ? Theme.accent :
                                                                                Theme.textSecondary
                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
            ScrollView {
                id: fallbackLyrics
                anchors.fill: parent
                visible: lyricList.count === 0
                clip: true
                contentWidth: availableWidth
                Label {
                    width: fallbackLyrics.availableWidth
                    padding: 16
                    text: root.player.lyrics || "这首歌暂时没有同步歌词"
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.pixelSize: root.font.pixelSize * 1.3
                    lineHeight: 1.5
                    color: Theme.textSecondary
                }
            }
        }
        Button {
            text: "回到当前歌词"
            visible: root.browsingLyrics
            Layout.alignment: Qt.AlignHCenter
            onClicked: {
                root.browsingLyrics = false;
                root.followLyrics();
            }
        }
    }

}
