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
    property Item themeHost: null
    property bool queueAvailable: false
    property bool queueActive: false
    property string activePage: ""
    property Item pageReturnFocusItem: null
    property bool exiting: false
    property bool hasPlaylists: false
    property bool hasDownloads: false
    property bool hasBrowser: false
    property bool downloadsExitRecovery: false
    property bool preferencesOpen: false
    property alias nowButton: nowAction
    property alias queueButton: queueAction
    property alias preferencesButton: preferencesAction
    property alias playlistsButton: playlistsAction
    property alias downloadsButton: downloadsAction
    property alias browserButton: browserAction
    property alias themeButton: themeAction
    signal nowRequested
    signal queueRequested
    signal preferencesRequested
    signal playlistsRequested
    signal downloadsRequested
    signal browserRequested
    signal themeRequested

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
                checkable: true
                checked: root.activePage === "playlists"
                visible: root.hasPlaylists
                enabled: visible && !root.exiting
                onClicked: root.playlistsRequested()
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
                text: downloadLabels.title
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
                visible: root.hasBrowser
                enabled: visible && !root.exiting
                onClicked: root.browserRequested()
            }
            Item {
                Layout.fillHeight: true
            }
            Item {
                parent: root.themeHost !== null ? root.themeHost : navigationLayout
                implicitWidth: root.themeHost !== null ? root.themeHost.width : themeAction.implicitWidth
                implicitHeight: root.themeHost !== null ? root.themeHost.height : themeAction.implicitHeight
                Layout.fillWidth: true
                AccessibleTransportButton {
                    id: themeAction
                    objectName: "theme-toggle"
                    anchors.fill: parent
                    KeyNavigation.tab: null
                    KeyNavigation.backtab: null
                    contract: root.contract
                    text: root.contract.darkTheme ? root.contract.themeLight : root.contract.themeDark
                    iconName: root.editorial ? (root.contract.darkTheme ? "sun" : "moon") : ""
                    iconWithText: true
                    accessibleName: root.contract.darkTheme ? labels.themeLight : labels.themeDark
                    enabled: !root.exiting
                    onClicked: root.themeRequested()
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
                text: root.contract.preferencesTitle
                checkable: true
                checked: root.preferencesOpen
                enabled: !root.exiting
                onClicked: root.preferencesRequested()
            }
        }
    }
}
