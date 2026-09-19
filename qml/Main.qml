pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "theme"
import "pages"
import "components"

ApplicationWindow {
    id: window
    required property var collectionViewModel
    required property var commentsViewModel
    required property var discoverViewModel
    required property var favorites
    required property var appSettings
    required property var playbackController
    required property var sessionManager
    required property var searchViewModel
    required property var libraryViewModel

    readonly property int homePage: 0
    readonly property int searchPage: 1
    readonly property int libraryPage: 2
    readonly property int noOverlay: -1
    readonly property int nowPlayingOverlay: 0
    readonly property int settingsOverlay: 1
    readonly property int collectionOverlay: 2
    readonly property bool iosPlatform: Qt.platform.os === "ios"
    readonly property bool mobilePlatform: Qt.platform.os === "android" || iosPlatform
    property var pendingAddTrack: ({})
    property var navigationHistory: []
    property int currentPage: Math.max(homePage, Math.min(libraryPage, appSettings.lastPage))
    property int overlayPage: noOverlay
    readonly property bool compact: width < 600
    readonly property bool typing: activeFocusItem instanceof TextInput
                                   || activeFocusItem instanceof TextEdit
    onCurrentPageChanged: appSettings.lastPage = currentPage
    Component.onCompleted: {
        if (mobilePlatform)
            showMaximized();
        if (appSettings.activeServiceUrl.length === 0)
            overlayPage = settingsOverlay;
    }

    function requestAddTrack(track) {
        pendingAddTrack = track;
        if (!sessionManager.authenticated) {
            loginDialog.open();
            return;
        }
        libraryViewModel.load();
        playlistPicker.track = track;
        playlistPicker.open();
        pendingAddTrack = ({});
    }
    PlaylistPicker {
        id: playlistPicker
        library: window.libraryViewModel
    }
    Connections {
        target: window.collectionViewModel
        function onScopeReset() {
            window.navigationHistory = [];
            if (window.overlayPage === window.collectionOverlay)
                window.overlayPage = window.noOverlay;
        }
    }
    function rememberPage() {
        const detailPage = overlayLoader.item as CollectionPage;
        if (overlayPage === collectionOverlay && detailPage)
            detailPage.savePosition();
        navigationHistory = navigationHistory.concat([
                                                         {
                                                             page: overlayPage,
                                                             collection: overlayPage === collectionOverlay
                                                                         ? collectionViewModel.captureState(
                                                                               ) : {}
                                                         }
                                                     ]).slice(-12);
    }
    function openOverlay(page) {
        if (overlayPage === page)
            return;
        rememberPage();
        overlayPage = page;
    }
    function openAlbum(track) {
        if (!track.albumId)
            return;
        rememberPage();
        collectionViewModel.open(track);
    }
    function openArtist(artist) {
        if (!artist.id)
            return;
        rememberPage();
        collectionViewModel.openArtist(artist);
    }
    function openEntry(entry) {
        if (entry.kind === "album")
            openAlbum(entry);
        else if (entry.kind === "artist")
            openArtist(entry);
        else if (entry.kind === "rank") {
            rememberPage();
            collectionViewModel.openRank(entry);
        } else {
            rememberPage();
            collectionViewModel.openPlaylist(entry);
        }
    }
    function navigateBack() {
        if (navigationHistory.length === 0) {
            overlayPage = noOverlay;
            return;
        }
        const previous = navigationHistory[navigationHistory.length - 1];
        navigationHistory = navigationHistory.slice(0, -1);
        if (previous.page === collectionOverlay)
            collectionViewModel.restoreState(previous.collection);
        overlayPage = previous.page;
    }

    width: mobilePlatform ? Screen.width : 1100
    height: mobilePlatform ? Screen.height : 720
    minimumWidth: mobilePlatform ? 0 : 390
    minimumHeight: mobilePlatform ? 0 : 600
    flags: Qt.Window | (iosPlatform ? Qt.ExpandedClientAreaHint : 0)
    visible: true
    color: Theme.background
    title: "音乐"
    palette.window: Theme.background
    palette.base: Theme.surface
    palette.button: Theme.surface
    palette.buttonText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.windowText: Theme.textPrimary
    palette.highlight: Theme.accent
    Binding {
        target: Theme
        property: "dark"
        value: window.appSettings.darkTheme
    }
    Shortcut {
        sequence: "Space"
        enabled: !window.typing && !loginDialog.opened && !playlistPicker.opened
        onActivated: window.playbackController.togglePlayback()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: !window.typing
        onActivated: window.playbackController.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: !window.typing
        onActivated: window.playbackController.previous()
    }
    LoginDialog {
        id: loginDialog
        sessionManager: window.sessionManager
        onClosed: {
            window.favorites.cancelPending();
            if (window.sessionManager.authenticated && window.pendingAddTrack.key)
                window.requestAddTrack(window.pendingAddTrack);
            else
                window.pendingAddTrack = ({});
        }
    }
    header: ToolBar {
        visible: window.compact && window.overlayPage === window.noOverlay
        contentHeight: 56
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: window.currentPage === window.homePage ? "发现" :
                  window.currentPage === window.searchPage ? "搜索" : "音乐库"
            font.pixelSize: 20
            font.bold: true
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            ToolButton {
                text: "设置"
                onClicked: window.openOverlay(window.settingsOverlay)
            }
            ToolButton {
                text: window.sessionManager.authenticated ? "账号" : "登录"
                onClicked: window.sessionManager.authenticated
                           ? window.openOverlay(window.settingsOverlay) :
                                                                   loginDialog.open()
            }
        }
    }
    QueueDrawer {
        id: queueDrawer
        player: window.playbackController
    }
    Popup {
        id: actionToast
        property string message: ""
        x: Math.max(16, (window.width - width) / 2)
        y: window.height - height - (window.compact ? 92 : 24)
        width: Math.min(440, window.width - 32)
        modal: false
        closePolicy: Popup.NoAutoClose
        padding: 14
        contentItem: Label {
            text: actionToast.message
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Timer {
            id: actionToastTimer
            interval: 5000
            onTriggered: actionToast.close()
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Pane {
            Layout.preferredWidth: 212
            Layout.fillHeight: true
            visible: !window.compact && window.overlayPage !== window.nowPlayingOverlay
            padding: 16
            background: Rectangle {
                color: Theme.surface
                border.color: Theme.border
            }
            ColumnLayout {
                width: parent.width
                NavigationTab {
                    text: "发现"
                    checkable: true
                    checked: window.currentPage === window.homePage
                    Layout.fillWidth: true
                    onClicked: window.currentPage = window.homePage
                }
                NavigationTab {
                    text: "搜索"
                    checkable: true
                    checked: window.currentPage === window.searchPage
                    Layout.fillWidth: true
                    onClicked: window.currentPage = window.searchPage
                }
                NavigationTab {
                    text: "音乐库"
                    checkable: true
                    checked: window.currentPage === window.libraryPage
                    Layout.fillWidth: true
                    onClicked: window.currentPage = window.libraryPage
                }
                Item {
                    Layout.fillHeight: true
                }
                Button {
                    text: window.sessionManager.authenticated ? "酷狗账号已连接" : "登录酷狗"
                    Layout.fillWidth: true
                    onClicked: window.sessionManager.authenticated
                               ? window.openOverlay(window.settingsOverlay) :
                                                                      loginDialog.open()
                }
                Button {
                    text: "设置"
                    Layout.fillWidth: true
                    onClicked: window.openOverlay(window.settingsOverlay)
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            StackLayout {
                currentIndex: window.overlayPage === window.noOverlay ? 0 : 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                StackLayout {
                    currentIndex: window.currentPage
                    DiscoverPage {
                        showHeading: !window.compact
                        discoverViewModel: window.discoverViewModel
                        playbackController: window.playbackController
                        onEntryRequested: entry => window.openEntry(entry)
                        onAlbumRequested: track => window.openAlbum(track)
                        onAddToPlaylistRequested: track => window.requestAddTrack(track)
                    }
                    SearchPage {
                        showHeading: !window.compact
                        onEntryRequested: entry => window.openEntry(entry)
                        onAddToPlaylistRequested: track => window.requestAddTrack(track)
                        onAlbumRequested: track => window.openAlbum(track)
                        searchViewModel: window.searchViewModel
                        playbackController: window.playbackController
                        onLoginRequested: loginDialog.open()
                    }
                    LibraryPage {
                        showHeading: !window.compact
                        onAddToPlaylistRequested: track => window.requestAddTrack(track)
                        onAlbumRequested: track => window.openAlbum(track)
                        favorites: window.favorites
                        libraryViewModel: window.libraryViewModel
                        sessionManager: window.sessionManager
                        playbackController: window.playbackController
                        active: window.currentPage === window.libraryPage
                        onLoginRequested: loginDialog.open()
                    }
                }
                Loader {
                    id: overlayLoader
                    active: window.overlayPage !== window.noOverlay
                    sourceComponent: window.overlayPage === window.nowPlayingOverlay
                                     ? nowPlayingComponent : window.overlayPage === window.collectionOverlay
                                                               ? collectionComponent : settingsComponent
                }
            }
            PlayerBar {
                playbackController: window.playbackController
                visible: window.playbackController.hasCurrentTrack &&
                         window.overlayPage !== window.nowPlayingOverlay
                Layout.fillWidth: true
                onDetailRequested: window.openOverlay(window.nowPlayingOverlay)
                onQueueRequested: queueDrawer.open()
            }
            TabBar {
                visible: window.compact && window.overlayPage === window.noOverlay
                currentIndex: window.currentPage
                Layout.fillWidth: true
                onCurrentIndexChanged: window.currentPage = currentIndex
                NavigationTab {
                    text: "发现"
                }
                NavigationTab {
                    text: "搜索"
                }
                NavigationTab {
                    text: "音乐库"
                }
            }
        }
    }
    Connections {
        target: window.favorites
        function onLoginRequested() {
            loginDialog.open();
        }
    }
    function openComments(kind, id, title) {
        if (!id)
            return;
        commentsViewModel.open(kind, id, title);
        commentsDialog.open();
    }
    CommentsDialog {
        id: commentsDialog
        commentsViewModel: window.commentsViewModel
        sessionManager: window.sessionManager
        onLoginRequested: loginDialog.open()
    }
    Connections {
        target: window.collectionViewModel
        function onLoginRequired() { loginDialog.open(); }
    }
    Connections {
        target: window.libraryViewModel
        function onActionChanged() {
            if (!window.libraryViewModel.actionBusy &&
                    window.libraryViewModel.actionMessage.length > 0) {
                actionToast.message = window.libraryViewModel.actionMessage;
                actionToast.open();
                if (!window.libraryViewModel.actionUncertain)
                    actionToastTimer.restart();
                else
                    actionToastTimer.stop();
            }
        }
    }
    Connections {
        target: window.playbackController
        function onDetailRequested() {
            window.openOverlay(window.nowPlayingOverlay);
        }
        function onNoticeChanged() {
            if (window.playbackController.notice.length === 0)
                return;
            actionToast.message = window.playbackController.notice;
            actionToast.open();
            actionToastTimer.restart();
        }
    }
    Component {
        id: nowPlayingComponent
        NowPlayingPage {
            onCommentsRequested: track => window.openComments("song", track.albumAudioId,
                                                              track.title)
            onArtistRequested: artist => window.openArtist(artist)
            onAlbumRequested: track => {
                window.openAlbum(track);
            }
            favorites: window.favorites
            player: window.playbackController
            onCloseRequested: window.navigateBack()
            onQueueRequested: queueDrawer.open()
        }
    }
    Connections {
        target: window.collectionViewModel
        function onOpened() {
            window.overlayPage = window.collectionOverlay;
        }
    }
    Component {
        id: collectionComponent
        CollectionPage {
            onCommentsRequested: (kind, id, title) => window.openComments(kind, id, title)
            onAddToPlaylistRequested: track => window.requestAddTrack(track)
            collection: window.collectionViewModel
            player: window.playbackController
            onCloseRequested: window.navigateBack()
        }
    }
    Component {
        id: settingsComponent
        SettingsPage {
            appSettings: window.appSettings
            playbackController: window.playbackController
            sessionManager: window.sessionManager
            onCloseRequested: window.navigateBack()
            onLoginRequested: loginDialog.open()
        }
    }
}
