pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"

Page {
    id: root
    required property var appSettings
    required property var playbackController
    required property var sessionManager

    signal closeRequested
    signal loginRequested
    property string serviceError: ""
    property string serviceFeedback: ""
    FileDialog {
        id: diagnosticsFile
        title: "导出诊断"
        fileMode: FileDialog.SaveFile
        nameFilters: ["文本文件 (*.txt)"]
        defaultSuffix: "txt"
        onRejected: root.appSettings.reportDiagnosticsCancelled()
        onAccepted: root.appSettings.exportDiagnostics(selectedFile,
                                                       root.playbackController.playbackStatus,
                                                       root.playbackController.storageError)
    }
    background: Rectangle {
        color: Theme.background
    }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            ToolButton {
                text: "返回"
                onClicked: root.closeRequested()
            }
            Label {
                text: "设置"
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
        }
    }
    Flickable {
        id: settingsScroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: settingsContent.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: ScrollBar {
            policy: settingsScroll.contentHeight > settingsScroll.height ? ScrollBar.AsNeeded
                                                                         : ScrollBar.AlwaysOff
        }
        ColumnLayout {
            id: settingsContent
            width: Math.min(720, settingsScroll.width - 32)
            x: Math.max(16, (settingsScroll.width - width) / 2)
            spacing: 20
            Label {
                text: "账号与服务"
                font.pixelSize: 22
                font.bold: true
                Layout.topMargin: 20
            }
            Frame {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        text: "酷狗渠道后端"
                        font.bold: true
                    }
                    Label {
                        text: "当前生效：" + (root.appSettings.activeServiceUrl.length > 0
                                            ? root.appSettings.activeServiceUrl : "未配置")
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                        color: Theme.textSecondary
                    }
                    Label {
                        visible: root.appSettings.environmentOverride
                        text: "当前地址由环境变量指定；下方保存值会在移除覆盖后生效。"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        color: Theme.textSecondary
                    }
                    TextField {
                        id: serviceField
                        Layout.fillWidth: true
                        text: root.appSettings.serviceUrl
                        placeholderText: "https://example.com/"
                        Accessible.name: "酷狗渠道后端地址"
                    }
                    Button {
                        text: "应用地址"
                        onClicked: {
                            root.serviceError = root.appSettings.validateServiceUrl(
                                        serviceField.text);
                            root.serviceFeedback = "";
                            if (!root.serviceError) {
                                root.appSettings.serviceUrl = serviceField.text;
                                serviceField.text = root.appSettings.serviceUrl;
                                root.serviceFeedback = root.appSettings.environmentOverride
                                        ? "已保存；移除环境变量覆盖后生效。"
                                        : root.appSettings.activeServiceUrl.length > 0
                                          ? "已应用；账号凭据按服务地址隔离。"
                                          : "已清空渠道后端。";
                            }
                        }
                    }
                    Label {
                        visible: root.serviceError.length > 0 || root.serviceFeedback.length > 0
                        text: root.serviceError || root.serviceFeedback
                        color: root.serviceError ? Theme.error : Theme.textSecondary
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }
                    Label {
                        text: root.sessionManager.authenticated ? "酷狗账号已登录" : "当前未登录"
                    }
                    Label {
                        text: root.sessionManager.storageError
                        visible: text.length > 0
                        color: Theme.error
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Button {
                        visible: root.sessionManager.authenticated
                        text: "退出登录"
                        onClicked: root.sessionManager.logout()
                    }
                    Button {
                        visible: !root.sessionManager.authenticated
                        text: "填写 Cookie 或扫码登录"
                        highlighted: true
                        enabled: root.sessionManager.serviceConfigured
                        onClicked: root.loginRequested()
                    }
                }
            }
            Label {
                text: "外观与播放"
                font.pixelSize: 22
                font.bold: true
            }
            Frame {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        text: "主题"
                    }
                    ComboBox {
                        model: ["跟随系统", "浅色", "深色"]
                        currentIndex: root.appSettings.themeMode === "dark" ? 2 :
                                                                              root.appSettings.themeMode
                                                                              === "light" ? 1 : 0
                        onActivated: root.appSettings.themeMode = ["system", "light",
                                                                   "dark"][currentIndex]
                    }
                    Label {
                        text: "音量 " + root.playbackController.volume + "%"
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: root.playbackController.volume
                        onMoved: root.playbackController.volume = value
                    }
                    Label {
                        text: "优先音质"
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["标准 · 128 kbps", "高品 · 320 kbps", "无损 · FLAC"]
                        currentIndex: Math.max(0, ["128", "320", "flac"].indexOf(
                                                   root.appSettings.preferredQuality))

                        enabled: !root.appSettings.dataSaver
                        onActivated: root.appSettings.preferredQuality = ["128", "320",
                                                                          "flac"][currentIndex]
                    }
                    Switch {
                        text: "节省流量"
                        checked: root.appSettings.dataSaver
                        onToggled: root.appSettings.dataSaver = checked
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textSecondary
                        text: root.appSettings.dataSaver ? "节省流量时优先使用标准音质；关闭后恢复原选择。下次播放或重试时生效。" :
                                                           "下次播放或重试时生效；可用音质取决于歌曲和账号权限。播放页显示音频提供的实际码率。"
                    }
                }
            }
            Label {
                text: "存储"
                font.pixelSize: 22
                font.bold: true
            }
            Frame {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        text: "最近播放保留最多 100 条"
                        Layout.fillWidth: true
                    }
                    Button {
                        text: "清除历史"
                        onClicked: clearHistoryDialog.open()
                    }
                    Label {
                        text: "封面缓存 " + (root.appSettings.cacheBytes / 1048576).toFixed(1) + " MB"
                    }
                    Button {
                        text: "清除封面缓存"
                        onClicked: root.appSettings.clearArtworkCache()
                    }
                    Label {
                        text: root.playbackController.storageError
                        visible: text.length > 0
                        color: Theme.error
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            Label {
                text: "关于与诊断"
                font.pixelSize: 22
                font.bold: true
            }
            Frame {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        text: "音乐 " + Qt.application.version
                    }
                    Button {
                        text: "导出诊断"
                        onClicked: diagnosticsFile.open()
                    }
                    Label {
                        text: root.appSettings.storageMessage
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            Item {
                Layout.preferredHeight: 24
            }
        }
    }
    Dialog {
        id: clearHistoryDialog
        title: "清除最近播放"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(400, root.width - 32)
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            Label {
                text: "清除本机保存的最近播放记录？当前队列和正在播放的歌曲会保留。"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "取消"; onClicked: clearHistoryDialog.close() }
                Button {
                    text: "清除记录"
                    onClicked: {
                        root.playbackController.clearHistory();
                        clearHistoryDialog.close();
                    }
                }
            }
        }
    }
}
