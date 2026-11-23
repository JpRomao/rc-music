import QtQml

QtObject {
    readonly property string now: qsTr("Agora")
    readonly property string home: qsTr("Início")
    readonly property string pageDescription: qsTr("O que está tocando, sua fila e sua biblioteca.")
    readonly property string queueShelf: qsTr("Na sua fila")
    readonly property string queueCount: qsTr("Fila atual: %1")
    readonly property string seeQueue: qsTr("Ver fila")
    readonly property string detailsShort: qsTr("Detalhes")
    readonly property string localAudio: qsTr("Artista desconhecido")
    readonly property string playTrack: qsTr("Reproduzir %1")
    readonly property string loadingTrack: qsTr("PREPARANDO A FAIXA")
    readonly property string nowPlaying: qsTr("AGORA TOCANDO")
    readonly property string paused: qsTr("REPRODUÇÃO PAUSADA")
    readonly property string ready: qsTr("PRONTO PARA OUVIR")
    readonly property string playlists: qsTr("Playlists")
    function trackCount(count) {
        return qsTr("%n faixa", "Contagem da Biblioteca na lateral", count);
    }
    function playlistCount(count) {
        return qsTr("%n playlist", "Contagem de Playlists na lateral", count);
    }
    readonly property string library: qsTr("Biblioteca")
    readonly property string libraryEyebrow: qsTr("BIBLIOTECA / TODAS AS FAIXAS")
    readonly property string libraryDescription: qsTr("A fila muda. As faixas que você guarda continuam aqui.")
    readonly property string search: qsTr("Buscar")
    readonly property string searchEyebrow: qsTr("BUSCAR / YOUTUBE")
    readonly property string searchDescription: qsTr("Encontre uma música e baixe o áudio para a sua biblioteca.")
    readonly property string listenTogether: qsTr("Ouvir junto")
    readonly property string listenTogetherEyebrow: qsTr("OUVIR JUNTO / SALA DE ESCUTA")
    readonly property string listenTogetherDescription: qsTr("Abra uma sala e ouça a mesma música, ao mesmo tempo, com quem você convidar.")
    readonly property string space: qsTr("SEU ESPAÇO MUSICAL")
    readonly property string heading: qsTr("Bom ter sua música por perto.")
    readonly property string themeLight: qsTr("Ativar tema claro")
    readonly property string themeDark: qsTr("Ativar tema escuro")
    readonly property string breadcrumbRoot: qsTr("Seu espaço")
    readonly property string breadcrumbSeparator: qsTr("/")
    readonly property string activity: qsTr("Atividade")
    readonly property string settings: qsTr("Configurações")
    readonly property string closeActivity: qsTr("Fechar atividade")
    readonly property string closeSettings: qsTr("Fechar configurações")
    readonly property string playlistsEyebrow: qsTr("SUA COLEÇÃO / PLAYLISTS")
    readonly property string playlistsDescription: qsTr("Listas que você monta e guarda, sem mexer na fila.")
    readonly property string activityEyebrow: qsTr("DOWNLOADS E IMPORTAÇÕES")
    readonly property string activityDescription: qsTr("Acompanhe o que está sendo baixado e o que precisa da sua atenção.")
    readonly property string settingsEyebrow: qsTr("PREFERÊNCIAS DO APLICATIVO")
    readonly property string settingsDescription: qsTr("Aparência, reprodução, arquivos e integrações.")
}
