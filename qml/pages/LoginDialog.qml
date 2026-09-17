pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Dialog {
    id: root
    required property var sessionManager

    title: "连接酷狗账号"
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(380, Overlay.overlay.width - 32)
    height: Math.min(560, Overlay.overlay.height - 32)
    anchors.centerIn: Overlay.overlay
    onOpened: {
        cookieField.clear();
        root.sessionManager.cancelQrLogin();
    }
    onClosed: root.sessionManager.cancelQrLogin()
    Connections {
        target: root.sessionManager
        function onStateChanged() {
            if (root.sessionManager.authenticated && root.opened)
                root.close();
        }
    }
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                text: "Cookie 登录"
                font.bold: true
            }
            Label {
                text: "填写包含 token 和 userid 的 Cookie，可直接使用账号会话。应用不会回显已保存的内容。"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TextField {
                id: cookieField
                Layout.fillWidth: true
                placeholderText: "token=...; userid=..."
                echoMode: TextInput.Password
                enabled: root.sessionManager.serviceConfigured
                Accessible.name: "酷狗账号 Cookie"
                onAccepted: root.sessionManager.loginWithCookie(text)
            }
            Button {
                text: "使用 Cookie"
                highlighted: true
                enabled: root.sessionManager.serviceConfigured && cookieField.text.length > 0
                Layout.fillWidth: true
                onClicked: root.sessionManager.loginWithCookie(cookieField.text)
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }
            Label {
                text: "扫码登录"
                font.bold: true
            }
            BusyIndicator {
                running: root.sessionManager.loading
                visible: running
                Layout.alignment: Qt.AlignHCenter
            }
            Image {
                source: root.sessionManager.qrImage
                visible: source.toString().length > 0
                fillMode: Image.PreserveAspectFit
                Layout.preferredWidth: Math.min(300, root.width - 64)
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment: Qt.AlignHCenter
                Accessible.name: "酷狗登录二维码"
            }
            Label {
                text: root.sessionManager.message
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
            Button {
                text: root.sessionManager.qrImage.length > 0 ? "刷新二维码" : "生成二维码"
                visible: !root.sessionManager.authenticated
                enabled: root.sessionManager.canRefresh
                onClicked: root.sessionManager.startQrLogin()
                Layout.fillWidth: true
            }
        }
    }
}
