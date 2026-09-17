import QtQuick
import QtQuick.Controls
import "../theme"

Rectangle {
    id: root
    property string coverUrl: ""
    property string entityName: ""
    property string imageKind: "封面"
    property bool circular: false
    property int pixelSize: Math.ceil(width * Screen.devicePixelRatio)
    readonly property int requestedSize: pixelSize <= 150 ? 150 : pixelSize <= 400 ? 400 : 800
    color: Theme.surface
    radius: circular ? width / 2 : 8
    border.color: Theme.border
    implicitWidth: 48
    implicitHeight: 48
    clip: true
    Image {
        id: artwork
        anchors.fill: parent
        source: root.coverUrl.replace("{size}", root.requestedSize.toString())
        sourceSize: Qt.size(root.requestedSize, root.requestedSize)
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        cache: true
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: 140
            }
        }
    }
    Label {
        anchors.centerIn: parent
        visible: artwork.status !== Image.Ready
        text: root.width < 88 ? "♪" : artwork.status === Image.Loading ? "加载中" :
                                artwork.status === Image.Error ? "图片未加载" : "暂无图片"
        font.pixelSize: Math.min(12, root.width / 5)
        color: Theme.textSecondary
    }
    Accessible.role: Accessible.Graphic
    Accessible.name: entityName + imageKind
    Accessible.description: artwork.status === Image.Ready ? "图片已加载" : artwork.status === Image.Error ? "图片加载失败" :
                                                                                                        artwork.status
                                                                                                        === Image.Loading
                                                                                                        ? "图片加载中" :
                                                                                                          "暂无图片"
}
