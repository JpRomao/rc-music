import QtQml

QtObject {
    readonly property string backwardTemplate: qsTr("Voltar %1")
    readonly property string forwardTemplate: qsTr("Avançar %1")
    readonly property string previousShortcut: qsTr("Shift+← (ou Ctrl+←)")
    readonly property string backwardShortcut: qsTr("←")
    readonly property string playPauseShortcut: qsTr("Espaço")
    readonly property string forwardShortcut: qsTr("→")
    readonly property string nextShortcut: qsTr("Shift+→ (ou Ctrl+→)")
    readonly property string muteShortcut: qsTr("M")
    readonly property string seekShortcutHint: qsTr("←/→: saltos configurados; ↑/↓: volume; Page Up/Page Down: 10 segundos; Home/End: início/fim.")
    readonly property string seekRequiresTrack: qsTr("Abra uma faixa para alterar a posição.")
    readonly property string seekLoading: qsTr("Aguarde a faixa ficar pronta para alterar a posição.")
    readonly property string seekInactive: qsTr("Não é possível mudar a posição agora.")
    readonly property string seekUnsupported: qsTr("Esta fonte não permite avançar ou voltar.")
    readonly property string seekPreviewActive: qsTr("Conclua o ajuste da posição antes de usar o salto.")
    readonly property string seekPending: qsTr("Aguarde a atualização da posição antes de usar o salto.")
    readonly property string lyricsAction: qsTr("Letra")
    readonly property string lyricsUnavailable: qsTr("Esta faixa ainda não tem letra.")
    readonly property string queueWithCount: qsTr("Abrir a fila (%1)")
    readonly property string volumePercent: qsTr("%1%")
    readonly property string elapsedOfDuration: qsTr("%1 / %2")
    readonly property string artistAlbum: qsTr("%1 · %2")

    function seconds(count) {
        return qsTr("%n segundo", "Duração configurável do salto", count);
    }

    function backwardName(count) {
        return backwardTemplate.replace("%1", seconds(count));
    }

    function forwardName(count) {
        return forwardTemplate.replace("%1", seconds(count));
    }
}
