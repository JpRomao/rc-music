import QtQml

// Strings of the permanent library page (ADR-0019).
QtObject {
    readonly property string filterPlaceholder: qsTr("Filtrar a biblioteca…")
    readonly property string filterName: qsTr("Filtrar a biblioteca")
    readonly property string coversView: qsTr("Capas")
    readonly property string listView: qsTr("Lista")
    readonly property string viewChoice: qsTr("Modo de exibição da biblioteca")
    readonly property string numberHeader: qsTr("Nº")
    readonly property string trackHeader: qsTr("FAIXA / ARTISTA")
    readonly property string albumHeader: qsTr("ÁLBUM")
    readonly property string timeHeader: qsTr("DURAÇÃO")
    readonly property string unknownArtist: qsTr("Artista desconhecido")
    readonly property string unavailable: qsTr("Indisponível")
    readonly property string unavailableReason: qsTr("Arquivo não encontrado. Localize o arquivo para tocar a faixa.")
    readonly property string subtitleSeparator: qsTr("·")
    readonly property string rowHint: qsTr("Clique duas vezes para tocar.")
    readonly property string playNow: qsTr("Tocar agora")
    readonly property string playNowName: qsTr("Tocar %1 agora")
    readonly property string playNowDescription: qsTr("Entra logo depois da faixa atual e começa a tocar.")
    readonly property string playNowShortcut: qsTr("Enter")
    readonly property string playNowFailed: qsTr("Não foi possível tocar agora. Tente novamente.")
    readonly property string enqueueAction: qsTr("Adicionar à fila")
    readonly property string enqueueDescription: qsTr("Entra no fim da fila.")
    readonly property string enqueue: qsTr("Adicionar %1 à fila")
    readonly property string enqueued: qsTr("“%1” foi para o fim da fila.")
    readonly property string enqueueFailed: qsTr("Não foi possível adicionar à fila agora. Tente novamente.")
    readonly property string addToPlaylistAction: qsTr("Adicionar à playlist")
    readonly property string addToPlaylist: qsTr("Adicionar %1 a uma playlist")
    readonly property string addedToPlaylist: qsTr("“%1” foi para a playlist “%2”.")
    readonly property string addToPlaylistFailed: qsTr("Não foi possível adicionar à playlist agora. Confira a página Playlists e tente novamente.")
    readonly property string noPlaylists: qsTr("Nenhuma playlist ainda")
    readonly property string locateAction: qsTr("Localizar arquivo…")
    readonly property string locate: qsTr("Localizar %1")
    readonly property string locateShort: qsTr("Localizar")
    readonly property string removeAction: qsTr("Remover da biblioteca…")
    readonly property string remove: qsTr("Remover %1 da biblioteca")
    readonly property string removeTitle: qsTr("Remover “%1” da biblioteca?")
    readonly property string removeAdvice: qsTr("A faixa sai só da biblioteca. O arquivo, a fila, as playlists e o que está tocando continuam como estão.")
    readonly property string removeConfirm: qsTr("Remover da biblioteca")
    readonly property string emptyTitle: qsTr("Sua biblioteca está vazia")
    readonly property string emptyAdvice: qsTr("Abra arquivos do computador ou baixe faixas em Buscar. Tudo fica guardado aqui, mesmo quando a fila muda.")
    readonly property string noMatches: qsTr("Nenhuma faixa corresponde a “%1”.")
    readonly property string retryLoad: qsTr("Tentar carregar novamente")
    readonly property string retrySave: qsTr("Tentar salvar novamente")

    // The count line while a filter is typed: matches of the total; the plural form follows the total.
    function filteredCount(visible, total) {
        return qsTr("%1 de %n faixa", "Contagem da Biblioteca com filtro", total).arg(visible);
    }
    // The count line under the page title: all the tracks, or those that match the filter.
    function countLine(visible, total, filtered) {
        return filtered ? filteredCount(visible, total) : navigation.trackCount(total);
    }
    // The screen reader reads title and artist (an unknown artist by its own name), then the unavailable mark.
    function rowName(title, artist, available) {
        const named = navigation.fillTwo(navigation.titleAndArtist, title, artist.length > 0 ? artist : unknownArtist);
        return available ? named : navigation.fillTwo(navigation.titleAndArtist, named, unavailable);
    }
    function rowDescription(available) {
        return available ? rowHint : unavailableReason;
    }
    // The description of a home shelf card: the playing mark, the reason it is unavailable, or nothing.
    function cardDescription(playing, available) {
        if (playing)
            return navigation.playingBadge;
        if (!available)
            return unavailableReason;
        return "";
    }

    property NavigationText navigation: NavigationText {}
}
