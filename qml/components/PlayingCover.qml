pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

CoverImage {
    id: root
    property bool current: false
    property bool playing: false
    Rectangle {
        objectName: "currentTrackIndicator"
        anchors.fill: parent
        visible: root.current
        color: "#80000000"
        radius: root.radius
        Accessible.role: Accessible.StaticText
        Accessible.name: root.playing ? "正在播放" : "当前歌曲，已暂停"
        Row {
            anchors.centerIn: parent
            spacing: 3
            Repeater {
                model: root.playing ? [9, 18, 13] : [14, 14]
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
