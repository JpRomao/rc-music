pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

// Dialogo modal Aurora: veu na janela inteira, superficie elevada, titulo,
// conteudo rolavel e acoes alinhadas a direita. Devolve o foco a quem abriu.
// Cabecalho, corpo e rodape ficam numa so coluna, na ordem visual: e essa a
// ordem de Tab e de leitura.
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
    // Controle que recebe o foco ao abrir. Vazio, vale o primeiro controle do
    // corpo, depois o botao de fechar e por fim a primeira acao do rodape.
    property Item initialFocusItem: null

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
    onOpened: {
        const target = initialFocusItem ? initialFocusItem : firstFocusTarget();
        if (target)
            target.forceActiveFocus(Qt.TabFocusReason);
    }
    onClosed: {
        if (previousFocus)
            previousFocus.forceActiveFocus();
        previousFocus = null;
    }

    function contains(area, item) {
        for (let node = item; node; node = node.parent) {
            if (node === area)
                return true;
        }
        return false;
    }
    function firstInside(area) {
        const next = area.nextItemInFocusChain(true);
        return next && contains(area, next) ? next : null;
    }
    function firstFocusTarget() {
        const body = firstInside(bodyColumn);
        if (body)
            return body;
        if (closeButton.visible)
            return closeButton;
        return firstInside(footerRow);
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

    // Cabecalho e rodape vivem dentro do conteudo, nao nas areas do Dialog.
    header: null
    footer: null

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            objectName: "dialog-header"
            spacing: 8
            Layout.fillWidth: true

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
                id: closeButton
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

        ScrollView {
            id: scroll
            objectName: "dialog-scroll"
            clip: true
            contentWidth: availableWidth
            contentHeight: bodyColumn.implicitHeight + 2 * dialog.ringRoom
            implicitHeight: contentHeight + topPadding + bottomPadding
            leftPadding: dialog.margin - dialog.ringRoom
            rightPadding: dialog.margin - dialog.ringRoom
            topPadding: 0
            bottomPadding: 16 - dialog.ringRoom
            Layout.fillWidth: true
            Layout.fillHeight: true

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

        Item {
            objectName: "dialog-footer"
            implicitHeight: footerRow.children.length > 0 ? footerRow.implicitHeight + 24 : 0
            visible: footerRow.children.length > 0
            Layout.fillWidth: true

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
}
