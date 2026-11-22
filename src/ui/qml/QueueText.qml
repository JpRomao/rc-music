import QtQuick

QtObject {
    readonly property string drawerEyebrow: qsTr("Reprodução")
    function trackCount(count) {
        return qsTr("%n faixa", "Contagem de faixas da fila", count);
    }
    readonly property string durationMinutes: qsTr("%1 min")
    readonly property string durationHours: qsTr("%1 h %2 min")
    readonly property string countAndDuration: qsTr("%1 · %2")
    readonly property string closeQueue: qsTr("Fechar fila")
    readonly property string addFiles: qsTr("Adicionar arquivos…")
    readonly property string removeFromQueue: qsTr("Remover da fila")
    readonly property string clearEllipsis: qsTr("Limpar fila…")
    readonly property string clearName: qsTr("Limpar a fila")
    readonly property string manageLyrics: qsTr("Gerenciar letras")
    readonly property string add: qsTr("Adicionar arquivos")
    readonly property string moveUp: qsTr("Mover para cima")
    readonly property string moveDown: qsTr("Mover para baixo")
    readonly property string remove: qsTr("Remover")
    readonly property string clear: qsTr("Limpar fila")
    readonly property string shuffle: qsTr("Modo aleatório")
    readonly property string shuffleOn: qsTr("Aleatório: Ligado")
    readonly property string shuffleOff: qsTr("Aleatório: Desligado")
    readonly property string repeat: qsTr("Repetir fila")
    readonly property string repeatOn: qsTr("Repetir: Ligada")
    readonly property string repeatOff: qsTr("Repetir: Desligada")
    readonly property string shortcutShuffle: qsTr("F1")
    readonly property string shortcutRepeat: qsTr("F2")
    readonly property string selectionShortcut: qsTr("Ctrl+↑/↓: selecionar faixa; Enter: tocar.")
    readonly property string playNow: qsTr("Tocar agora")
    readonly property string retry: qsTr("Tentar reproduzir novamente")
    readonly property string retryDescription: qsTr("Tenta abrir o mesmo arquivo novamente, preservando sua posição na fila.")
    readonly property string retrying: qsTr("Tentando reproduzir novamente…")
    readonly property string locate: qsTr("Localizar arquivo…")
    readonly property string locateDescription: qsTr("Atualiza o local desta ocorrência sem interromper a faixa em reprodução.")
    readonly property string locating: qsTr("Localizando arquivo…")
    readonly property string locationFailed: qsTr("Não foi possível localizar o arquivo. Tente novamente.")
    readonly property string sourceRelocated: qsTr("Novo arquivo selecionado. Tente reproduzir para verificar.")
    readonly property string clearTitle: qsTr("Limpar fila?")
    readonly property string clearOneMessage: qsTr("O item da fila será removido. Esta ação não pode ser desfeita.")
    readonly property string clearManyMessage: qsTr("Os {count} itens da fila serão removidos. Esta ação não pode ser desfeita.")
    readonly property string cancel: qsTr("Cancelar")
    readonly property string movedAnnouncement: qsTr("{title} movida para a posição {position} de {total}.")
    readonly property string positionOfTotal: qsTr("posição {position} de {total}")
    readonly property string sourceUnavailable: qsTr("Arquivo não disponível.")
    readonly property string sourceVolumeRemoved: qsTr("Unidade desconectada.")
    readonly property string invalidMedia: qsTr("Arquivo inválido ou não suportado.")
    readonly property string accessDenied: qsTr("Acesso ao arquivo negado.")
    readonly property string openRejected: qsTr("Não foi possível abrir o arquivo.")
    readonly property string unavailable: qsTr("Faixa indisponível.")
    readonly property string shortcutMoveUp: qsTr("Alt+↑")
    readonly property string shortcutMoveDown: qsTr("Alt+↓")
    readonly property string shortcutRemove: qsTr("Delete")
    readonly property string hostControls: qsTr("O host controla")
    readonly property string hostNotAllowed: qsTr("O host não liberou")
    readonly property string guestLocalQueueHeld: qsTr("Durante a sala você ouve a fila do anfitrião. A sua fila volta quando você sair.")
    readonly property string roomQueue: qsTr("Fila da sala")
    readonly property string loading: qsTr("Carregando…")
    readonly property string addedBy: qsTr("Adicionada por %1")
    readonly property string loadingAddedBy: qsTr("Carregando… · adicionada por %1")

    function unavailableReason(errorCode) {
        switch (errorCode) {
        case "source_unavailable":
        case "fixture_source_unavailable":
            return sourceUnavailable;
        case "source_volume_removed":
            return sourceVolumeRemoved;
        case "invalid_media":
            return invalidMedia;
        case "access_denied":
            return accessDenied;
        case "open_rejected":
            return openRejected;
        case "source_relocated":
            return sourceRelocated;
        default:
            return unavailable;
        }
    }
}
