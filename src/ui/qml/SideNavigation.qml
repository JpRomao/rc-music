import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Rectangle {
    id: root
    objectName: "side-navigation"
    required property ShellUiContract contract
    property bool compact: false
    property bool editorial: false
    property bool queueAvailable: false
    property bool queueActive: false
    property string activePage: ""
    property Item pageReturnFocusItem: null
    property bool exiting: false
    property bool hasPlaylists: false
    property bool hasLibrary: false
    property bool hasSearch: false
    property bool hasRoom: false
    property bool hasDownloads: false
    property bool hasBrowser: false
    property bool downloadsExitRecovery: false
    // Counts come from the bindings; below zero means unknown and shows nothing.
    property int libraryCount: -1
    property int playlistCount: -1
    property bool preferencesOpen: false
    property var profile: null
    property alias nowButton: nowAction
    property alias queueButton: queueAction
    property alias preferencesButton: preferencesAction
    property alias playlistsButton: playlistsAction
    property alias libraryButton: libraryAction
    property alias searchButton: searchAction
    property alias roomButton: roomAction
    property alias downloadsButton: downloadsAction
    property alias browserButton: browserAction
    readonly property Item themeButton: themeLoader.item as Item
    property alias profileChip: profileAction
    signal nowRequested
    signal queueRequested
    signal preferencesRequested
    signal playlistsRequested
    signal libraryRequested
    signal searchRequested
    signal roomRequested
    signal downloadsRequested
    signal browserRequested
    signal themeRequested
    signal profileRequested

    color: contract.navigationColor
    NavigationText {
        id: labels
    }
    DownloadText {
        id: downloadLabels
    }
    BrowserText {
        id: browserLabels
    }
    Rectangle {
        anchors.right: parent.right
        height: parent.height
        width: 1
        color: root.editorial ? root.contract.dividerColor : root.contract.borderColor
        opacity: root.contract.highContrast || root.editorial ? 1 : 0.4
    }
    function revealFocus(item) {
        if (!item || !item.activeFocus)
            return;
        let ancestor = item;
        while (ancestor && ancestor !== root)
            ancestor = ancestor.parent;
        if (!ancestor)
            return;
        const point = item.mapToItem(navigationFlick.contentItem, 0, 0);
        if (point.y < navigationFlick.contentY)
            navigationFlick.contentY = Math.max(0, point.y);
        else if (point.y + item.height > navigationFlick.contentY + navigationFlick.height)
            navigationFlick.contentY = Math.min(navigationFlick.contentHeight - navigationFlick.height, point.y + item.height - navigationFlick.height);
    }
    Flickable {
        id: navigationFlick
        objectName: "navigation-viewport"
        anchors.fill: parent
        anchors.margins: root.compact ? 12 : 20
        contentWidth: width
        contentHeight: Math.max(height, navigationLayout.implicitHeight)
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: AccessibleScrollBar {
            contract: root.contract
        }
        ColumnLayout {
            id: navigationLayout
            width: navigationFlick.width
            height: navigationFlick.contentHeight
            spacing: root.editorial ? 5 : root.contract.spacingUnit * 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Row {
                    // Approved Aurora brand mark: three accent bars (12/24/18 px).
                    objectName: "navigation-brand-mark"
                    visible: root.editorial
                    Layout.alignment: root.compact ? Qt.AlignHCenter : Qt.AlignVCenter
                    spacing: 3
                    Accessible.ignored: true
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 12
                        radius: 2
                        color: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.accentColor
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 24
                        radius: 2
                        color: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.accentColor
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 18
                        radius: 2
                        color: root.contract.highContrast ? root.contract.textPrimaryColor : root.contract.accentColor
                    }
                }
                Text {
                    objectName: "navigation-brand"
                    Layout.fillWidth: true
                    visible: !(root.editorial && root.compact)
                    text: root.contract.appName
                    Accessible.name: root.contract.appName
                    textFormat: Text.PlainText
                    font.family: root.contract.headingFontFamily
                    font.pixelSize: root.editorial ? root.contract.typeSection : (root.compact ? 22 : 28)
                    font.bold: !root.editorial
                    color: root.contract.textPrimaryColor
                    elide: Text.ElideRight
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 16
                visible: !root.compact
                text: labels.space
                objectName: "navigation-caption"
                textFormat: Text.PlainText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: root.contract.typeEyebrow
                font.letterSpacing: 1
                color: root.contract.textSecondaryColor
            }
            NavigationButton {
                id: nowAction
                iconName: "home"
                editorial: root.editorial
                compact: root.compact
                objectName: "now"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Keys.onBacktabPressed: event => {
                    const target = root.pageReturnFocusItem;
                    if (target && target.visible && target.enabled) {
                        target.forceActiveFocus(Qt.BacktabFocusReason);
                        event.accepted = true;
                    } else {
                        event.accepted = false;
                    }
                }
                Layout.fillWidth: true
                contract: root.contract
                text: root.editorial ? labels.home : labels.now
                checkable: true
                checked: root.activePage.length > 0 ? root.activePage === "home" : !root.queueActive
                enabled: !root.exiting
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.nowRequested()
            }
            NavigationButton {
                id: queueAction
                iconName: "queue"
                editorial: root.editorial
                compact: root.compact
                objectName: "queue"
                visible: !root.editorial
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: root.contract.queueTitle
                checkable: true
                checked: root.queueActive
                enabled: root.queueAvailable && !root.exiting
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.queueRequested()
            }
            NavigationButton {
                id: libraryAction
                iconName: "library"
                editorial: root.editorial
                compact: root.compact
                objectName: "library"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: labels.library
                count: root.libraryCount
                countText: root.libraryCount >= 0 ? labels.trackCount(root.libraryCount) : ""
                checkable: true
                checked: root.activePage === "library"
                visible: root.hasLibrary && root.editorial
                enabled: visible && !root.exiting
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.libraryRequested()
            }
            NavigationButton {
                id: searchAction
                iconName: "search"
                editorial: root.editorial
                compact: root.compact
                objectName: "search"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: labels.search
                checkable: true
                checked: root.activePage === "search"
                visible: root.hasSearch && root.editorial
                enabled: visible && !root.exiting
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.searchRequested()
            }
            NavigationButton {
                id: playlistsAction
                iconName: "playlist"
                editorial: root.editorial
                compact: root.compact
                objectName: "playlists"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: labels.playlists
                count: root.playlistCount
                countText: root.playlistCount >= 0 ? labels.playlistCount(root.playlistCount) : ""
                checkable: true
                checked: root.activePage === "playlists"
                visible: root.hasPlaylists
                enabled: visible && !root.exiting
                onClicked: root.playlistsRequested()
            }
            NavigationButton {
                id: roomAction
                iconName: "people"
                editorial: root.editorial
                compact: root.compact
                objectName: "room"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: labels.listenTogether
                checkable: true
                checked: root.activePage === "room"
                visible: root.hasRoom && root.editorial
                enabled: visible && !root.exiting
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.roomRequested()
            }
            NavigationButton {
                id: downloadsAction
                iconName: "activity"
                editorial: root.editorial
                compact: root.compact
                objectName: "downloads"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: root.editorial ? labels.activity : downloadLabels.title
                checkable: true
                checked: root.activePage === "downloads"
                visible: root.hasDownloads
                enabled: visible && (!root.exiting || root.downloadsExitRecovery)
                onClicked: root.downloadsRequested()
            }
            NavigationButton {
                id: browserAction
                iconName: "browser"
                editorial: root.editorial
                compact: root.compact
                objectName: "embedded-browser"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: browserLabels.navigate
                trailingIconName: "external"
                visible: root.hasBrowser
                enabled: visible && !root.exiting
                onClicked: root.browserRequested()
            }
            Item {
                Layout.fillHeight: true
            }
            // O fixture congelado mantém o botão de tema aqui; o Aurora o tem no topo.
            Loader {
                id: themeLoader
                active: !root.editorial
                Layout.fillWidth: true
                sourceComponent: themeButtonComponent

                Component {
                    id: themeButtonComponent
                    AccessibleTransportButton {
                        objectName: "theme-toggle"
                        KeyNavigation.tab: null
                        KeyNavigation.backtab: null
                        contract: root.contract // qmllint disable unqualified
                        text: root.contract.darkTheme ? root.contract.themeLight : root.contract.themeDark // qmllint disable unqualified
                        iconWithText: true
                        accessibleName: root.contract.darkTheme ? labels.themeLight : labels.themeDark // qmllint disable unqualified
                        enabled: !root.exiting // qmllint disable unqualified
                        onClicked: root.themeRequested() // qmllint disable unqualified
                    }
                }
            }
            NavigationButton {
                id: preferencesAction
                iconName: "settings"
                editorial: root.editorial
                compact: root.compact
                objectName: "preferences"
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                Layout.fillWidth: true
                contract: root.contract
                text: root.editorial ? labels.settings : root.contract.preferencesTitle
                checkable: true
                checked: root.preferencesOpen
                enabled: !root.exiting
                onClicked: root.preferencesRequested()
            }
            ProfileChip {
                id: profileAction
                Layout.fillWidth: true
                contract: root.contract
                profile: root.profile
                compact: root.compact
                visible: root.editorial && root.profile !== null && root.profile.available === true
                enabled: !root.exiting
                KeyNavigation.tab: null
                KeyNavigation.backtab: null
                onClicked: root.profileRequested()
            }
        }
    }
}
