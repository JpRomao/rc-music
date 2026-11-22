import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraPiecesVisual"
    when: windowShown
    readonly property var contract: testProductUiContract // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified

    function cleanup() {
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
    }
    function themes() {
        return [
            {
                tag: "claro",
                dark: false
            },
            {
                tag: "escuro",
                dark: true
            }
        ];
    }
    function luminance(color) {
        function linear(value) {
            return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }
    function verifyContrast(foreground, background, minimum, context) {
        const first = luminance(foreground);
        const second = luminance(background);
        const ratio = (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
        verify(ratio >= minimum, context + ": " + ratio + " < " + minimum);
    }

    function test_piecePairsInBothThemes_data() {
        return themes();
    }
    function test_piecePairsInBothThemes(data) {
        driver.setSystemAppearance(data.dark, false);
        const pairs = [["accentTextColor", "accentColor", 4.5], ["textPrimaryColor", "selectionColor", 4.5], ["errorColor", "selectionColor", 4.5], ["accentTextColor", "errorColor", 4.5], ["accentColor", "selectionColor", 4.5], ["accentColor", "surfaceColor", 4.5], ["textPrimaryColor", "hoverColor", 4.5], ["textPrimaryColor", "surfaceRaisedColor", 4.5], ["textSecondaryColor", "surfaceRaisedColor", 4.5], ["textSecondaryColor", "surfaceColor", 4.5], ["warningColor", "selectionColor", 4.5], ["successColor", "selectionColor", 4.5], ["errorColor", "surfaceRaisedColor", 4.5], ["accentColor", "surfaceRaisedColor", 4.5], ["textSecondaryColor", "canvasColor", 4.5], ["accentColor", "canvasColor", 3], ["focusColor", "selectionColor", 3], ["focusColor", "canvasColor", 3], ["errorColor", "hoverColor", 4.5], ["accentColor", "hoverColor", 4.5], ["successColor", "canvasColor", 4.5], ["successColor", "surfaceColor", 4.5], ["successColor", "surfaceRaisedColor", 4.5], ["warningColor", "canvasColor", 4.5], ["warningColor", "surfaceColor", 4.5], ["warningColor", "surfaceRaisedColor", 4.5], ["errorColor", "canvasColor", 4.5], ["errorColor", "surfaceColor", 4.5], ["focusColor", "surfaceColor", 3], ["focusColor", "surfaceRaisedColor", 3]];
        for (const p of pairs)
            verifyContrast(Qt.color(contract[p[0]]), Qt.color(contract[p[1]]), p[2], data.tag + " " + p[0] + " sobre " + p[1]);
    }

    Component {
        id: galleryHost
        Window {
            id: win
            width: 1100
            height: 1500
            visible: true
            color: testCase.contract.canvasColor
            property alias gallery: gallery

            Flow {
                id: gallery
                objectName: "gallery"
                width: win.width
                spacing: 24

                ColumnLayout {
                    spacing: 12
                    RowLayout {
                        spacing: 8
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-primary"
                            kind: AuroraButton.Primary
                            text: "Principal"
                        }
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-tonal"
                            kind: AuroraButton.Tonal
                            text: "Tonal"
                        }
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-quiet"
                            kind: AuroraButton.Quiet
                            text: "Discreto"
                        }
                    }
                    RowLayout {
                        spacing: 8
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-danger"
                            kind: AuroraButton.Danger
                            text: "Perigo"
                        }
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-danger-filled"
                            kind: AuroraButton.DangerFilled
                            text: "Excluir"
                        }
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-disabled"
                            enabled: false
                            text: "Desabilitado"
                        }
                        AuroraButton {
                            contract: testCase.contract
                            controlId: "g-compact"
                            compact: true
                            kind: AuroraButton.Tonal
                            text: "Compacto"
                        }
                    }
                    RowLayout {
                        spacing: 8
                        AuroraIconButton {
                            contract: testCase.contract
                            controlId: "g-icon-on"
                            iconName: "shuffle"
                            checkable: true
                            checked: true
                        }
                        AuroraIconButton {
                            contract: testCase.contract
                            controlId: "g-icon-off"
                            iconName: "shuffle"
                            checkable: true
                        }
                    }
                }
                ColumnLayout {
                    spacing: 12
                    AuroraTextField {
                        contract: testCase.contract
                        controlId: "g-field-empty"
                        label: "Apelido"
                        placeholderText: "Nome"
                        Layout.preferredWidth: 260
                    }
                    AuroraTextField {
                        id: focused
                        contract: testCase.contract
                        controlId: "g-field-focus"
                        label: "Sala"
                        text: "Sala da casa"
                        Layout.preferredWidth: 260
                        Component.onCompleted: input.forceActiveFocus(Qt.TabFocusReason)
                    }
                    AuroraTextField {
                        contract: testCase.contract
                        controlId: "g-field-error"
                        label: "Código"
                        text: "abc"
                        errorText: "Código inválido"
                        Layout.preferredWidth: 260
                    }
                    AuroraSelect {
                        contract: testCase.contract
                        controlId: "g-select"
                        label: "Saída de áudio"
                        model: ["Alto-falantes", "Fones"]
                        Layout.preferredWidth: 260
                    }
                }
                ColumnLayout {
                    spacing: 12
                    AuroraSegmented {
                        contract: testCase.contract
                        controlId: "g-segmented"
                        label: "Exibição"
                        options: ["Lista", "Capas", "Álbuns"]
                    }
                    RowLayout {
                        spacing: 12
                        AuroraSwitch {
                            contract: testCase.contract
                            controlId: "g-switch-on"
                            label: "Ligado"
                            checked: true
                        }
                        AuroraSwitch {
                            contract: testCase.contract
                            controlId: "g-switch-off"
                            label: "Desligado"
                        }
                        AuroraCheckBox {
                            contract: testCase.contract
                            controlId: "g-check"
                            checked: true
                            text: "Marcada"
                        }
                        AuroraCheckBox {
                            contract: testCase.contract
                            controlId: "g-check-off"
                            text: "Livre"
                        }
                    }
                    AuroraStepper {
                        contract: testCase.contract
                        controlId: "g-stepper"
                        label: "Valor"
                    }
                    RowLayout {
                        spacing: 8
                        AuroraChip {
                            contract: testCase.contract
                            text: "Neutro"
                            tone: AuroraChip.Neutral
                        }
                        AuroraChip {
                            contract: testCase.contract
                            text: "Destaque"
                            tone: AuroraChip.Accent
                        }
                        AuroraChip {
                            contract: testCase.contract
                            text: "Aviso"
                            tone: AuroraChip.Warning
                        }
                        AuroraChip {
                            contract: testCase.contract
                            text: "Pronto"
                            tone: AuroraChip.Success
                        }
                        AuroraChip {
                            contract: testCase.contract
                            text: "Erro"
                            tone: AuroraChip.Error
                        }
                    }
                    RowLayout {
                        spacing: 12
                        AuroraAvatar {
                            contract: testCase.contract
                            source: "qrc:/rc_music/avatars/avatar-01.svg"
                            backgroundColor: testCase.contract.canvasColor
                        }
                        AuroraAvatar {
                            contract: testCase.contract
                            source: "qrc:/rc_music/avatars/avatar-02.svg"
                            backgroundColor: testCase.contract.canvasColor
                            selected: true
                        }
                        AuroraAvatar {
                            contract: testCase.contract
                            backgroundColor: testCase.contract.canvasColor
                        }
                    }
                }
                ColumnLayout {
                    spacing: 12
                    AuroraNotice {
                        contract: testCase.contract
                        controlId: "g-notice-warning"
                        tone: AuroraNotice.Warning
                        title: "Atenção"
                        text: "Aviso de exemplo."
                        primaryActionText: "Entendi"
                        Layout.preferredWidth: 380
                    }
                    AuroraNotice {
                        contract: testCase.contract
                        controlId: "g-notice-error"
                        tone: AuroraNotice.Error
                        title: "Falha"
                        text: "Erro de exemplo."
                        primaryActionText: "Tentar de novo"
                        secondaryActionText: "Fechar"
                        Layout.preferredWidth: 380
                    }
                    AuroraNotice {
                        contract: testCase.contract
                        controlId: "g-notice-info"
                        tone: AuroraNotice.Info
                        title: "Informação"
                        text: "Informação de exemplo."
                        Layout.preferredWidth: 380
                    }
                    AuroraFeedback {
                        contract: testCase.contract
                        text: "Tudo certo"
                        tone: AuroraFeedback.Success
                    }
                }
                ColumnLayout {
                    spacing: 12
                    AuroraEmptyState {
                        contract: testCase.contract
                        controlId: "g-empty"
                        iconName: "shuffle"
                        title: "Nada por aqui"
                        hint: "Adicione músicas para começar."
                        primaryActionText: "Adicionar"
                        Layout.preferredWidth: 380
                    }
                    AuroraDrawerHeader {
                        contract: testCase.contract
                        controlId: "g-drawer"
                        closeText: "Fechar"
                        eyebrow: "Fila"
                        title: "Próximas"
                        subtitle: "12 faixas"
                        Layout.preferredWidth: 380
                    }
                    AuroraFloatingCard {
                        contract: testCase.contract
                        Layout.preferredWidth: 380
                        Layout.preferredHeight: 80
                        Label {
                            text: "Cartão flutuante"
                            color: testCase.contract.textPrimaryColor
                        }
                    }
                }
            }
        }
    }

    function test_galleryRenders_data() {
        return [
            {
                tag: "claro",
                dark: false,
                high: false
            },
            {
                tag: "escuro",
                dark: true,
                high: false
            },
            {
                tag: "alto-contraste",
                dark: false,
                high: true
            }
        ];
    }
    function test_galleryRenders(data) {
        driver.setSystemAppearance(data.dark, false);
        if (data.high)
            driver.setHighContrast(true, true);
        const win = createTemporaryObject(galleryHost, testCase);
        verify(win);
        waitForRendering(win.contentItem);
        const gallery = win.gallery;
        verify(gallery.height > 0);
        verify(gallery.childrenRect.height > 0);
        const path = driver.artifactPath("aurora-pecas-" + data.tag + ".png");
        if (path.length > 0) {
            const image = grabImage(win.contentItem);
            image.save(path);
        }
    }
}
