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
    readonly property int settingsPage: 1
    readonly property int libraryPage: 2
    readonly property int noOverlay: -1
    readonly property int nowPlayingOverlay: 0
    readonly property int collectionOverlay: 2
    readonly property bool iosPlatform: Qt.platform.os === "ios"
    readonly property bool mobilePlatform: Qt.platform.os === "android" || iosPlatform
    property var pendingAddTrack: ({})
    property var navigationHistory: []
    property int currentPage: appSettings.lastPage === libraryPage ? libraryPage : homePage
    property int overlayPage: noOverlay
    readonly property bool compact: width < 760
    property bool searchActive: false
    readonly property bool canGoBack: overlayPage !== noOverlay || searchActive ||
                                      (currentPage === libraryPage && libraryContent.canGoBack)
    readonly property bool modalOpen: loginDialog.opened || playlistPicker.opened ||
                                      commentsDialog.opened || queueDrawer.opened ||
                                      libraryContent.modalOpen || settingsContent.modalOpen ||
                                      Theme.openMenuCount > 0
    readonly property bool typing: activeFocusItem instanceof TextInput
                                   || activeFocusItem instanceof TextEdit
    onCurrentPageChanged: if (currentPage !== settingsPage) appSettings.lastPage = currentPage
    Component.onCompleted: {
        if (mobilePlatform)
            showMaximized();
        if (appSettings.activeServiceUrl.length === 0)
            currentPage = settingsPage;
    }

    function selectPage(page) {
        if (page !== homePage && page !== libraryPage && page !== settingsPage)
            return;
        searchContent.cancelPendingSearch();
        pageHeader.releaseSearchFocus();
        searchActive = false;
        overlayPage = noOverlay;
        navigationHistory = [];
        currentPage = page;
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
        pageHeader.releaseSearchFocus();
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
        pageHeader.releaseSearchFocus();
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
        if (modalOpen)
            return;
        pageHeader.releaseSearchFocus();
        if (overlayPage === collectionOverlay && collectionViewModel.goBack()) {
            Qt.callLater(() => {
                if (overlayLoader.item)
                    overlayLoader.item.restorePosition();
            });
            return;
        }
        if (overlayPage !== noOverlay) {
            if (navigationHistory.length === 0) {
                overlayPage = noOverlay;
                return;
            }
            const previous = navigationHistory[navigationHistory.length - 1];
            navigationHistory = navigationHistory.slice(0, -1);
            if (previous.page === collectionOverlay)
                collectionViewModel.restoreState(previous.collection);
            overlayPage = previous.page;
        } else if (searchActive) {
            searchContent.cancelPendingSearch();
            searchActive = false;
        } else if (currentPage === libraryPage) {
            libraryContent.navigateBack();
        }
    }

    width: mobilePlatform ? Screen.width : 1100
    height: mobilePlatform ? Screen.height : 720
    minimumWidth: mobilePlatform ? 0 : 390
    minimumHeight: mobilePlatform ? 0 : 600
    flags: Qt.Window | (iosPlatform ? Qt.ExpandedClientAreaHint : 0)
    visible: true
    color: Theme.background
    title: "音乐"
    font.pixelSize: 14
    palette.window: Theme.background
    palette.base: Theme.surface
    palette.button: Theme.surface
    palette.buttonText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.windowText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.placeholderText: Theme.textSecondary
    palette.mid: Theme.border
    palette.dark: Theme.border
    Binding {
        target: Theme
        property: "dark"
        value: window.appSettings.darkTheme
    }
    Shortcut {
        sequence: "Space"
        enabled: !window.typing && !window.modalOpen
        onActivated: window.playbackController.togglePlayback()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: !window.typing && !window.modalOpen
        onActivated: window.playbackController.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: !window.typing && !window.modalOpen
        onActivated: window.playbackController.previous()
    }
    LoginDialog {
        id: loginDialog
        objectName: "loginDialog"
        sessionManager: window.sessionManager
        onClosed: {
            window.favorites.cancelPending();
            if (window.sessionManager.authenticated && window.pendingAddTrack.key)
                window.requestAddTrack(window.pendingAddTrack);
            else
                window.pendingAddTrack = ({});
        }
    }
    Shortcut {
        sequences: ["Escape", "Back", Qt.platform.os === "osx" ? "Meta+[" : "Alt+Left"]
        objectName: "navigationShortcut"
        enabled: window.canGoBack && !window.modalOpen
        onActivated: window.navigateBack()
    }
    QueueDrawer {
        id: queueDrawer
        objectName: "queueDrawer"
        player: window.playbackController
    }
    Popup {
        id: actionToast
        property string message: ""
        x: Math.max(16, (window.width - width) / 2)
        y: Math.max(8, window.contentItem.height - height - 16
                    - (playerBar.visible ? playerBar.height : 0)
                    - (bottomNavigation.visible ? bottomNavigation.height : 0))
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
            Layout.preferredWidth: 208
            Layout.fillHeight: true
            visible: !window.compact && window.overlayPage !== window.nowPlayingOverlay
            padding: 16
            background: Rectangle {
                color: Theme.surface
                border.color: Theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Label {
                    text: "音乐"
                    font.pixelSize: 24
                    font.bold: true
                    Layout.leftMargin: 16
                    Layout.topMargin: 20
                    Layout.bottomMargin: 32
                }
                NavigationTab {
                    text: "发现"
                    checkable: true
                    sideNavigation: true
                    checked: window.currentPage === window.homePage
                    Layout.fillWidth: true
                    onClicked: window.selectPage(window.homePage)
                }
                NavigationTab {
                    text: "音乐库"
                    checkable: true
                    sideNavigation: true
                    checked: window.currentPage === window.libraryPage
                    Layout.fillWidth: true
                    onClicked: window.selectPage(window.libraryPage)
                }
                Item {
                    Layout.fillHeight: true
                }
                NavigationTab {
                    text: "设置"
                    checkable: true
                    sideNavigation: true
                    checked: window.currentPage === window.settingsPage
                    Layout.fillWidth: true
                    onClicked: window.selectPage(window.settingsPage)
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            PageHeader {
                id: pageHeader
                Layout.fillWidth: true
                canGoBack: window.canGoBack
                searchVisible: window.overlayPage === window.noOverlay
                searching: window.searchActive && window.overlayPage === window.noOverlay
                query: window.searchViewModel.query
                title: window.overlayPage === window.nowPlayingOverlay ? "正在播放" :
                       window.overlayPage === window.collectionOverlay ?
                           (window.collectionViewModel.kind === "artist" ? "歌手" :
                            window.collectionViewModel.kind === "album" ? "专辑" : "歌单") :
                       window.currentPage === window.homePage ? "发现" :
                       window.currentPage === window.libraryPage ? libraryContent.pageTitle : "设置"
                moreVisible: window.overlayPage === window.nowPlayingOverlay &&
                             window.playbackController.hasCurrentTrack
                onBackRequested: window.navigateBack()
                onSearchStarted: window.searchActive = true
                onQueryEdited: (query, composing) => {
                    window.searchActive = true;
                    searchContent.editQuery(query, composing);
                }
                onSearchSubmitted: {
                    searchContent.submit();
                    releaseSearchFocus();
                }
                onMoreRequested: anchor => {
                    anchor.actionMenu = playingMenu;
                    playingMenu.openAt(anchor);
                }
                ActionMenu {
                    id: playingMenu
                    objectName: "playingActionMenu"
                    ActionMenuItem {
                        text: "加入歌单"
                        onTriggered: window.requestAddTrack(window.playbackController.currentTrack)
                    }
                    ActionMenuItem {
                        text: "歌曲评论"
                        visible: !!window.playbackController.currentTrack.albumAudioId
                        onTriggered: window.openComments("song", window.playbackController.currentTrack.albumAudioId,
                                                        window.playbackController.title)
                    }
                }
            }
            Item {
                id: pageArea
                objectName: "pageArea"
                Layout.fillWidth: true
                Layout.fillHeight: true
                StackLayout {
                    anchors.fill: parent
                    currentIndex: window.overlayPage !== window.noOverlay ? 2 : window.searchActive ? 1 : 0
                    StackLayout {
                        currentIndex: window.currentPage
                        DiscoverPage {
                            showHeading: false
                            discoverViewModel: window.discoverViewModel
                            playbackController: window.playbackController
                            onEntryRequested: entry => window.openEntry(entry)
                            onAlbumRequested: track => window.openAlbum(track)
                            onAddToPlaylistRequested: track => window.requestAddTrack(track)
                        }
                        SettingsPage {
                            id: settingsContent
                            objectName: "settingsContent"
                            appSettings: window.appSettings
                            playbackController: window.playbackController
                            sessionManager: window.sessionManager
                            onLoginRequested: loginDialog.open()
                        }
                        LibraryPage {
                            id: libraryContent
                            objectName: "libraryContent"
                            showHeading: false
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
                    SearchPage {
                        id: searchContent
                        objectName: "searchContent"
                        showHeading: false
                        showSearchInput: false
                        searchFocused: pageHeader.searchFocused
                        onEntryRequested: entry => window.openEntry(entry)
                        onAddToPlaylistRequested: track => window.requestAddTrack(track)
                        onAlbumRequested: track => window.openAlbum(track)
                        searchViewModel: window.searchViewModel
                        playbackController: window.playbackController
                        onLoginRequested: loginDialog.open()
                    }
                    Loader {
                        id: overlayLoader
                        active: window.overlayPage !== window.noOverlay
                        sourceComponent: window.overlayPage === window.nowPlayingOverlay
                                         ? nowPlayingComponent : collectionComponent
                    }
                }
                EdgeBackGesture {
                    objectName: "leftBackGesture"
                    anchors.left: parent.left
                    height: parent.height
                    enabled: window.compact && window.canGoBack && !window.modalOpen
                    onBackRequested: window.navigateBack()
                }
                EdgeBackGesture {
                    objectName: "rightBackGesture"
                    anchors.right: parent.right
                    height: parent.height
                    fromRight: true
                    enabled: window.compact && window.canGoBack && !window.modalOpen
                    onBackRequested: window.navigateBack()
                }
            }
            PlayerBar {
                id: playerBar
                playbackController: window.playbackController
                visible: window.playbackController.hasCurrentTrack &&
                         window.overlayPage !== window.nowPlayingOverlay
                Layout.fillWidth: true
                onDetailRequested: window.openOverlay(window.nowPlayingOverlay)
                onQueueRequested: queueDrawer.open()
            }
            TabBar {
                id: bottomNavigation
                visible: window.compact && window.overlayPage === window.noOverlay
                currentIndex: [window.homePage, window.libraryPage, window.settingsPage].indexOf(window.currentPage)
                Layout.fillWidth: true
                NavigationTab {
                    text: "发现"
                    onClicked: window.selectPage(window.homePage)
                }
                NavigationTab {
                    text: "音乐库"
                    onClicked: window.selectPage(window.libraryPage)
                }
                NavigationTab {
                    text: "设置"
                    onClicked: window.selectPage(window.settingsPage)
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
            onArtistRequested: artist => window.openArtist(artist)
            onAlbumRequested: track => {
                window.openAlbum(track);
            }
            favorites: window.favorites
            player: window.playbackController
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
        }
    }
}
