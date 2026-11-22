pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// Dialogo modal Aurora: veu na janela inteira, superficie elevada, titulo,
// conteudo rolavel e acoes alinhadas a direita. Devolve o foco a quem abriu.
Dialog {
    id: dialog

    required property ShellUiContract contract
    required property string controlId
    property bool closable: true
    property int preferredWidth: 480
    // Conteudo do corpo, que rola quando passa da altura disponivel.
    default property alias content: bodyColumn.data
    // Botoes do rodape, alinhados a direita.
    property alias actions: footerRow.data

    property Item previousFocus: null

    readonly property real margin: 24
    // Espaço do anel de foco em volta de um controle: folga de 2 px mais o anel.
    readonly property real ringRoom: contract.focusRingWidth + 2

    ControlsText {
        id: controlsText
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: Math.min(preferredWidth, parent ? parent.width - 2 * margin : preferredWidth)
    height: Math.min(implicitHeight, parent ? parent.height - 2 * margin : implicitHeight)

    onAboutToShow: previousFocus = parent ? parent.Window.activeFocusItem : null
    onClosed: {
        if (previousFocus)
            previousFocus.forceActiveFocus();
        previousFocus = null;
    }

    Overlay.modal: Rectangle {
        objectName: "dialog-scrim"
        color: dialog.contract.scrimColor
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: dialog.contract.panelTransitionMs
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: dialog.contract.panelTransitionMs
        }
    }

    background: Rectangle {
        radius: dialog.contract.radiusLarge
        color: dialog.contract.highContrast ? dialog.contract.canvasColor : dialog.contract.surfaceRaisedColor
        border.width: dialog.contract.highContrast ? 2 : 0
        border.color: dialog.contract.textPrimaryColor

        AuroraShadow {
            contract: dialog.contract
            radius: parent.radius
        }
    }

    header: RowLayout {
        spacing: 8

        Text {
            objectName: "dialog-title"
            text: dialog.title
            color: dialog.contract.textPrimaryColor
            font.family: dialog.contract.headingFontFamily
            font.pixelSize: dialog.contract.typeSection
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            Accessible.role: Accessible.Heading
            Accessible.name: text
            Layout.fillWidth: true
            Layout.leftMargin: dialog.margin
            Layout.topMargin: 20
            Layout.bottomMargin: 8
            Layout.rightMargin: dialog.closable ? 0 : dialog.margin
        }
        AuroraIconButton {
            visible: dialog.closable
            contract: dialog.contract
            controlId: dialog.controlId + "-close"
            iconName: "close"
            text: controlsText.close
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 12
            Layout.rightMargin: 12
            onClicked: dialog.close()
        }
    }

    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        contentHeight: bodyColumn.implicitHeight + 2 * dialog.ringRoom
        implicitHeight: contentHeight + topPadding + bottomPadding
        leftPadding: dialog.margin - dialog.ringRoom
        rightPadding: dialog.margin - dialog.ringRoom
        topPadding: 0
        bottomPadding: 16 - dialog.ringRoom

        // A rolagem recorta no seu interior: o corpo fica recuado o bastante
        // para o anel de foco dos controles caber dentro do recorte.
        Column {
            id: bodyColumn
            x: dialog.ringRoom
            y: dialog.ringRoom
            width: scroll.availableWidth - 2 * dialog.ringRoom
            spacing: 12
        }
    }

    footer: Item {
        implicitHeight: footerRow.children.length > 0 ? footerRow.implicitHeight + 24 : 0
        visible: footerRow.children.length > 0

        Rectangle {
            width: parent.width
            height: 1
            color: dialog.contract.dividerColor
        }
        RowLayout {
            id: footerRow
            anchors.right: parent.right
            anchors.rightMargin: dialog.margin
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
        }
    }
}
