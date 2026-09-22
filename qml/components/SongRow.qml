pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

ItemDelegate {
    id: root
    required property var playbackController

    required property string trackKey
    required property string title
    required property string artistText
    required property string durationText
    required property string coverUrl
    required property string albumText
    required property var trackData
    property bool artworkAlwaysVisible: false
    property bool removable: false
    signal albumRequested(var track)
    signal addToPlaylistRequested(var track)
    signal removeRequested
    readonly property bool current: trackKey === playbackController.trackKey
    readonly property bool inViewport: visible && (artworkAlwaysVisible ||
                                      (!!ListView.view && y + height >= ListView.view.contentY &&
                                       y <= ListView.view.contentY + ListView.view.height))
    onInViewportChanged: playbackController.setArtworkVisible(trackData, inViewport)
    onTrackKeyChanged: if (inViewport)
                           playbackController.setArtworkVisible(trackData, true)
    Component.onCompleted: playbackController.setArtworkVisible(trackData, inViewport)
    Component.onDestruction: playbackController.setArtworkVisible(trackData, false)
    leftPadding: 10
    rightPadding: 6
    height: Math.max(72, contentItem.implicitHeight + 16)
    onClicked: root.playbackController.playSong(root.trackData)
    Accessible.name: title + "，" + artistText
    background: Rectangle {
        radius: Theme.radius
        color: root.current || root.down || root.hovered ? Theme.hoveredSurface : "transparent"
        border.width: root.visualFocus ? 1 : 0
        border.color: Theme.accent
    }
    contentItem: RowLayout {
        spacing: 12
        CoverImage {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            coverUrl: root.coverUrl
            entityName: root.title
            pixelSize: 100
            objectName: "songArtwork"
            Rectangle {
                objectName: "currentTrackIndicator"
                anchors.fill: parent
                visible: root.current
                color: "#80000000"
                radius: parent.radius
                Accessible.role: Accessible.StaticText
                Accessible.name: root.playbackController.playing ? "正在播放" : "当前歌曲，已暂停"
                Row {
                    anchors.centerIn: parent
                    spacing: 3
                    Repeater {
                        model: root.playbackController.playing ? [9, 18, 13] : [14, 14]
                        Rectangle {
                            required property int modelData
                            width: 3
                            height: modelData
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 1
                            color: "#FFFFFF"
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: root.title
                color: root.current ? Theme.accent : Theme.textPrimary
                font.pixelSize: 16
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: root.artistText
                color: Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Label {
            visible: root.width > 800
            text: root.albumText
            elide: Text.ElideRight
            Layout.maximumWidth: 180
            color: Theme.textSecondary
        }
        Label {
            visible: root.width > 450
            text: root.durationText
            color: Theme.textSecondary
        }
        IconButton {
            symbol: "more"
            text: "更多"
            implicitWidth: 48
            implicitHeight: 48
            onClicked: actions.open()
            Accessible.name: root.title + "的更多操作"
        }
    }
    Menu {
        id: actions
        MenuItem {
            text: "下一首播放"
            onTriggered: root.playbackController.enqueueSong(root.trackData, true)
        }
        MenuItem {
            text: "加入队列"
            onTriggered: root.playbackController.enqueueSong(root.trackData, false)
        }
        MenuItem {
            text: "加入歌单"
            onTriggered: root.addToPlaylistRequested(root.trackData)
        }
        MenuItem {
            text: "查看专辑"
            enabled: !!root.trackData.albumId
            onTriggered: root.albumRequested(root.trackData)
        }
        MenuSeparator { visible: root.removable }
        MenuItem {
            visible: root.removable
            text: "从此歌单移除"
            onTriggered: root.removeRequested()
        }
    }
}
