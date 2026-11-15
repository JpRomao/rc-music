import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Rectangle {
    id: root
    objectName: "side-navigation"
    required property ShellUiContract contract
    property bool compact: false
    property bool queueAvailable: false
    property bool queueActive: false
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
        color: root.contract.borderColor
        opacity: root.contract.highContrast ? 1 : 0.4
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
            spacing: root.contract.spacingUnit * 2
            Text {
                objectName: "navigation-brand"
                Layout.fillWidth: true
                text: root.contract.appName
                textFormat: Text.PlainText
                font.family: root.contract.headingFontFamily
                font.pixelSize: root.compact ? 22 : 28
                font.bold: true
                color: root.contract.textPrimaryColor
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 16
                visible: !root.compact
                text: labels.space
                objectName: "navigation-caption"
                textFormat: Text.PlainText
                font.family: root.contract.bodyFontFamily
                font.pixelSize: 10
                font.letterSpacing: 1
                color: root.contract.textSecondaryColor
            }
            AccessibleTransportButton {
                id: nowAction
                objectName: "now"
                Layout.fillWidth: true
                contract: root.contract
                text: labels.now
                checkable: true
                checked: !root.queueActive
                enabled: !root.exiting
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.nowRequested()
            }
            AccessibleTransportButton {
                id: queueAction
                objectName: "queue"
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
            AccessibleTransportButton {
                id: playlistsAction
                objectName: "playlists"
                Layout.fillWidth: true
                contract: root.contract
                text: labels.playlists
                visible: root.hasPlaylists
                enabled: visible && !root.exiting
                onClicked: root.playlistsRequested()
            }
            AccessibleTransportButton {
                id: downloadsAction
                objectName: "downloads"
                Layout.fillWidth: true
                contract: root.contract
                text: downloadLabels.title
                visible: root.hasDownloads
                enabled: visible && (!root.exiting || root.downloadsExitRecovery)
                onClicked: root.downloadsRequested()
            }
            AccessibleTransportButton {
                id: browserAction
                objectName: "embedded-browser"
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
            AccessibleTransportButton {
                id: themeAction
                objectName: "theme-toggle"
                Layout.fillWidth: true
                contract: root.contract
                text: root.contract.darkTheme ? root.contract.themeLight : root.contract.themeDark
                accessibleName: root.contract.darkTheme ? labels.themeLight : labels.themeDark
                enabled: !root.exiting
                onClicked: root.themeRequested()
            }
            AccessibleTransportButton {
                id: preferencesAction
                objectName: "preferences"
                Layout.fillWidth: true
                contract: root.contract
                text: root.contract.preferencesTitle
                checked: root.preferencesOpen
                enabled: !root.exiting
                onClicked: root.preferencesRequested()
            }
        }
    }
}
