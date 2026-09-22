pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."
import "../theme"

ToolBar {
    id: root
    property string title: ""
    property bool canGoBack: false
    property bool searchVisible: false
    property bool searching: false
    property string query: ""
    property bool moreVisible: false
    readonly property bool searchFocused: searchField.activeFocus
    signal backRequested()
    signal searchStarted()
    signal queryEdited(string query, bool composing)
    signal searchSubmitted()
    signal moreRequested()
    function releaseSearchFocus() {
        searchField.focus = false;
        Qt.inputMethod.hide();
    }
    implicitHeight: 72
    leftPadding: width < 600 ? 8 : 24
    rightPadding: width < 600 ? 16 : 32
    RowLayout {
        anchors.fill: parent
        spacing: 8
        IconButton {
            objectName: "navigationBack"
            symbol: "back"
            text: "返回上一页"
            visible: root.canGoBack
            onClicked: root.backRequested()
        }
        Label {
            visible: !root.searching
            text: root.title
            font.pixelSize: root.width < 600 ? 20 : 24
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: !root.searchVisible
            Layout.maximumWidth: root.searchVisible ? root.width * 0.35 : root.width
        }
        TextField {
            id: searchField
            objectName: "globalSearch"
            visible: root.searchVisible
            Layout.fillWidth: true
            placeholderText: "搜索音乐"
            text: root.searching ? root.query : ""
            rightPadding: clearSearch.visible ? 48 : 16
            Accessible.name: "搜索音乐"
            onActiveFocusChanged: if (activeFocus) root.searchStarted()
            onTextEdited: root.queryEdited(text, inputMethodComposing)
            onInputMethodComposingChanged: if (!inputMethodComposing && activeFocus)
                                              root.queryEdited(text, false)
            onAccepted: root.searchSubmitted()
            IconButton {
                id: clearSearch
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: searchField.text.length > 0
                symbol: "close"
                text: "清除搜索"
                onClicked: {
                    root.queryEdited("", false);
                    searchField.forceActiveFocus();
                }
            }
        }
        IconButton {
            visible: root.moreVisible
            symbol: "more"
            text: "更多操作"
            onClicked: root.moreRequested()
        }
    }
}
