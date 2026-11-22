import QtQuick
import MusicPlayer.M0

// Avatar redondo sem efeitos gráficos: uma capa na cor do fundo esconde os
// cantos quadrados da imagem. Sem imagem, ou se ela falhar, mostra um disco.
Item {
    id: root

    required property ShellUiContract contract
    property url source
    property int size: 40
    // Cor da capa dos cantos: tem de ser a cor do fundo de quem hospeda o avatar.
    property color backgroundColor: contract.surfaceColor
    property bool selected: false

    readonly property int coverWidth: Math.ceil(size / 4)
    readonly property bool showsFallback: source.toString().length === 0 || picture.status === Image.Error

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            objectName: "avatar-fallback"
            anchors.fill: parent
            radius: width / 2
            visible: root.showsFallback
            color: root.contract.highContrast ? root.contract.canvasColor : root.contract.selectionColor
            border.width: root.contract.highContrast ? 2 : 0
            border.color: root.contract.textPrimaryColor
        }
        Image {
            id: picture
            anchors.fill: parent
            source: root.source
            sourceSize: Qt.size(root.size * 2, root.size * 2)
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
            Accessible.ignored: true
        }
        Rectangle {
            objectName: "avatar-corner-cover"
            x: -root.coverWidth
            y: -root.coverWidth
            width: root.size + 2 * root.coverWidth
            height: width
            radius: root.size / 2 + root.coverWidth
            color: "transparent"
            border.width: root.coverWidth
            border.color: root.backgroundColor
        }
    }

    Rectangle {
        objectName: "avatar-selection-ring"
        anchors.fill: parent
        anchors.margins: -4
        radius: width / 2
        visible: root.selected
        color: "transparent"
        border.width: 2
        border.color: root.contract.accentColor
    }
}
