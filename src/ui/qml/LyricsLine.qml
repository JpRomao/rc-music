pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

// Aurora's verse line above the player. In verse mode it shows the current and next verse and opens the
// lyrics drawer; in notice mode (a pending version change) it shows the status and offers the review.
Rectangle {
    id: root

    required property ShellUiContract contract
    property string currentLine
    property string nextLine
    property string statusText
    property bool verseAvailable: false
    property bool notice: false
    property bool reviewAvailable: false
    property bool compact: false
    // The control the Tab order visits: the open button, or the review button in notice mode.
    readonly property Item entryItem: !shown ? null : (notice ? (reviewButton.visible ? reviewButton : null) : (openLoader.item as Item))
    readonly property bool shown: notice || verseAvailable

    signal openRequested(Item opener)
    signal reviewRequested

    objectName: "lyrics-line"
    visible: shown
    height: compact ? 40 : 48
    radius: 12
    color: contract.surfaceColor
    border.width: contract.highContrast ? 2 : 0
    border.color: contract.textPrimaryColor

    LyricsText {
        id: lyricsText
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 12 : 16
        anchors.rightMargin: root.compact ? 0 : 8
        spacing: 12

        ShellIcon {
            objectName: "lyrics-line-icon"
            iconName: "lyrics"
            size: 18
            color: root.contract.accentColor
            visible: !root.notice
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            objectName: "lyrics-line-current"
            visible: !root.notice
            text: root.currentLine
            textFormat: Text.PlainText
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: root.contract.headingFontFamily
            font.pixelSize: 17
            color: root.contract.textPrimaryColor
            Layout.fillWidth: root.compact
            Layout.maximumWidth: root.compact ? Number.POSITIVE_INFINITY : root.width * 0.48
            Layout.alignment: Qt.AlignVCenter
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Text {
            objectName: "lyrics-line-next"
            visible: !root.notice && !root.compact && text.length > 0
            text: root.nextLine
            textFormat: Text.PlainText
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: root.contract.bodyFontFamily
            font.pixelSize: 13
            color: root.contract.textSecondaryColor
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true
        }
        Item {
            visible: !root.notice && !root.compact && root.nextLine.length === 0
            Layout.fillWidth: true
        }
        Text {
            objectName: "lyrics-line-status"
            visible: root.notice
            text: root.statusText
            textFormat: Text.PlainText
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.family: root.contract.bodyFontFamily
            font.pixelSize: root.contract.typeBody
            color: root.contract.textSecondaryColor
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        AuroraButton {
            id: reviewButton
            contract: root.contract
            controlId: "lyrics-line-review"
            kind: AuroraButton.Tonal
            compact: true
            text: lyricsText.reviewChanges
            Accessible.name: lyricsText.reviewChangesName
            visible: root.notice && root.reviewAvailable
            onClicked: root.reviewRequested()
            Layout.alignment: Qt.AlignVCenter
        }
        // The two forms share an id, so only one of them is ever loaded.
        Loader {
            id: openLoader
            active: !root.notice
            visible: active
            sourceComponent: root.compact ? iconOpen : textOpen
            Layout.alignment: Qt.AlignVCenter
        }
    }

    Component {
        id: textOpen
        AuroraButton {
            id: textOpenButton
            contract: root.contract
            controlId: "lyrics-line-open"
            kind: AuroraButton.Quiet
            compact: true
            iconName: "arrow"
            text: lyricsText.openLyrics
            Accessible.name: lyricsText.openLyricsName
            onClicked: root.openRequested(textOpenButton)
        }
    }
    Component {
        id: iconOpen
        AuroraIconButton {
            id: iconOpenButton
            contract: root.contract
            controlId: "lyrics-line-open"
            iconName: "arrow"
            text: lyricsText.openLyricsName
            onClicked: root.openRequested(iconOpenButton)
        }
    }
}
