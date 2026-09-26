pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Dialog {
    id: root
    required property var commentsViewModel
    required property var sessionManager
    signal loginRequested
    title: "评论 · " + commentsViewModel.title
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, Overlay.overlay.width - 24)
    height: Math.min(760, Overlay.overlay.height - 32)
    padding: 16
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape

    Connections {
        target: root.commentsViewModel
        function onSent() { commentInput.text = ""; }
        function onLoginRequired() { root.loginRequested(); }
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "刷新"
                enabled: !root.commentsViewModel.loading
                onClicked: root.commentsViewModel.reload()
            }
        }
        Label {
            visible: root.commentsViewModel.errorMessage.length > 0
            text: root.commentsViewModel.errorMessage
            color: Theme.error
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ListView {
            id: commentList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: root.commentsViewModel.comments
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: commentRow
                required property var modelData
                width: commentList.width
                height: commentColumn.implicitHeight + 20
                color: Theme.surface
                radius: 8
                ColumnLayout {
                    id: commentColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: commentRow.modelData.author
                            textFormat: Text.PlainText
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            text: commentRow.modelData.time
                            textFormat: Text.PlainText
                            color: Theme.textSecondary
                            visible: text.length > 0
                        }
                    }
                    Label {
                        text: commentRow.modelData.content
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "赞 " + commentRow.modelData.likes
                        color: Theme.textSecondary
                        visible: commentRow.modelData.likes !== ""
                    }
                }
            }
            footer: Button {
                width: commentList.width
                visible: root.commentsViewModel.hasMore
                text: root.commentsViewModel.loading ? "加载中…" : "加载更多评论"
                enabled: !root.commentsViewModel.loading
                onClicked: root.commentsViewModel.loadMore()
            }
            Label {
                anchors.centerIn: parent
                visible: commentList.count === 0 && !root.commentsViewModel.loading &&
                         root.commentsViewModel.errorMessage.length === 0
                text: "暂无评论"
                color: Theme.textSecondary
            }
        }
        BusyIndicator {
            visible: root.commentsViewModel.loading && commentList.count === 0
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        TextArea {
            id: commentInput
            Layout.fillWidth: true
            Layout.preferredHeight: 86
            placeholderText: root.sessionManager.authenticated ? "写下评论（最多 500 字）" :
                                                                   "登录后可发表评论"
            wrapMode: TextEdit.Wrap
        }
        Label {
            text: root.commentsViewModel.sendMessage
            visible: text.length > 0
            color: root.commentsViewModel.sendUncertain ? Theme.error : Theme.textSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: commentInput.text.length + "/500"
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
            Button {
                text: root.sessionManager.authenticated ?
                          (root.commentsViewModel.sending ? "正在提交…" : "发表评论") : "登录后评论"
                enabled: !root.commentsViewModel.sending &&
                         !root.commentsViewModel.sendUncertain
                highlighted: true
                onClicked: root.sessionManager.authenticated ? confirmSend.open() :
                                                                 root.loginRequested()
            }
        }
    }

    Dialog {
        id: confirmSend
        title: "确认发表评论"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(400, root.width - 32)
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: root.commentsViewModel.send(commentInput.text)
        contentItem: Label {
            text: "评论将公开发布到酷狗。确认提交一次？"
            wrapMode: Text.Wrap
        }
    }
}
