import QtQuick
import QtQuick.Layouts
import MusicPlayer.M0

// Editorial page heading of the Aurora workspace: eyebrow, serif title and a
// short description. Trailing actions (e.g. a quiet close) go in `actions`.
ColumnLayout {
    id: root
    objectName: "page-header"
    required property ShellUiContract contract
    property string eyebrow: ""
    property string title: ""
    property string description: ""
    // Legacy (dialog) hosts keep their historical bold title.
    property bool editorial: true
    property int legacyTitleSize: contract.typeTitle
    default property alias actions: actionRow.data
    spacing: 6
    Text {
        objectName: "page-eyebrow"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.eyebrow
        textFormat: Text.PlainText
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeEyebrow
        font.letterSpacing: 1.2
        color: root.contract.textSecondaryColor
        elide: Text.ElideRight
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Text {
            objectName: "page-title"
            Layout.fillWidth: true
            text: root.title
            textFormat: Text.PlainText
            font.family: root.editorial ? root.contract.headingFontFamily : root.contract.bodyFontFamily
            font.pixelSize: root.editorial ? root.contract.typeHeading : root.legacyTitleSize
            font.bold: !root.editorial
            color: root.contract.textPrimaryColor
            elide: Text.ElideRight
            Accessible.role: root.editorial ? Accessible.Heading : Accessible.StaticText
            Accessible.name: text
        }
        RowLayout {
            id: actionRow
            spacing: 8
        }
    }
    Text {
        objectName: "page-description"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.description
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.family: root.contract.bodyFontFamily
        font.pixelSize: root.contract.typeBody
        color: root.contract.textSecondaryColor
    }
}
