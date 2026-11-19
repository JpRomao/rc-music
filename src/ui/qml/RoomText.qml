import QtQml

QtObject {
    readonly property string unavailable: qsTr("Sala indisponível neste build")
    readonly property string unavailableHint: qsTr("Esta versão do RC Music foi gerada sem o servidor de apresentação da sala.")
    readonly property string create: qsTr("Criar sala")
    readonly property string joinStart: qsTr("Entrar com link")
    readonly property string idleHint: qsTr("Crie uma sala e mande o convite, ou cole o link que alguém mandou para você.")
    readonly property string privacyTitle: qsTr("Antes da primeira sala")
    readonly property string privacyNotice: qsTr("Como em chamadas de voz, os participantes de uma sala ficam sabendo o endereço IP uns dos outros. Só convide quem você conhece.")
    readonly property string privacyAccept: qsTr("Entendi")
    readonly property string capacity: qsTr("Quantas pessoas podem entrar")
    readonly property string onePerson: qsTr("1 pessoa")
    readonly property string people: qsTr("%1 pessoas")
    readonly property string duration: qsTr("Duração do convite")
    readonly property string untilAppCloses: qsTr("Termina quando eu fechar o app")
    readonly property string untilRevoked: qsTr("Vale até eu revogar")
    readonly property string open: qsTr("Abrir sala")
    readonly property string back: qsTr("Voltar")
    readonly property string linkField: qsTr("Link de convite")
    readonly property string linkPlaceholder: qsTr("Cole aqui o link que você recebeu")
    readonly property string join: qsTr("Entrar")
    readonly property string preparing: qsTr("Preparando a sala…")
    readonly property string inviteTitle: qsTr("Convite")
    readonly property string copyLink: qsTr("Copiar link")
    readonly property string linkCopied: qsTr("Link copiado.")
    readonly property string hostCapacity: qsTr("Até %1 pessoas além de você")
    readonly property string membersTitle: qsTr("Na sala")
    readonly property string requestsTitle: qsTr("Pedidos para entrar")
    readonly property string requestName: qsTr("%1 quer entrar")
    readonly property string approve: qsTr("Aceitar")
    readonly property string reject: qsTr("Recusar")
    readonly property string approveName: qsTr("Aceitar %1")
    readonly property string rejectName: qsTr("Recusar %1")
    readonly property string hostTag: qsTr("anfitrião")
    readonly property string syncDrift: qsTr("diferença de sincronia: %1 ms")
    readonly property string selfTag: qsTr("você")
    readonly property string revoke: qsTr("Revogar convite")
    readonly property string close: qsTr("Fechar sala")
    readonly property string leave: qsTr("Sair da sala")
    readonly property string connecting: qsTr("Conectando à sala…")
    readonly property string waiting: qsTr("Esperando o anfitrião aceitar seu pedido…")
    readonly property string joined: qsTr("Você está na sala de %1.")
    readonly property string joinedUnnamed: qsTr("Você está na sala.")
    readonly property string outputOffset: qsTr("Ajuste de sincronia")
    readonly property string outputOffsetHint: qsTr("Se o seu som chega depois do som dos outros, como com fones Bluetooth, use um valor positivo para tocar antes.")
    readonly property string outputOffsetValue: qsTr("%1 ms")
    readonly property string reconnecting: qsTr("A conexão caiu. Tentando voltar à sala…")
    readonly property string dismissError: qsTr("Fechar aviso")
    readonly property string badge: qsTr("Na sala de %1 · %2 pessoas")
    readonly property string badgeSelf: qsTr("Sua sala · %1 pessoas")
    readonly property string rolesTitle: qsTr("Cargos")
    readonly property string rolesHint: qsTr("O que cada cargo pode fazer na sala. As mudanças valem na hora para quem está na sala.")
    readonly property string initialRole: qsTr("Cargo inicial do convite")
    readonly property string initialTag: qsTr("cargo inicial")
    readonly property string roleNameField: qsTr("Nome do cargo %1")
    readonly property string newRoleName: qsTr("Nome do novo cargo")
    readonly property string newRolePlaceholder: qsTr("Por exemplo, Amigos")
    readonly property string createRole: qsTr("Criar cargo")
    readonly property string removeRole: qsTr("Excluir cargo")
    readonly property string removeRoleName: qsTr("Excluir o cargo %1")
    readonly property string chooseAnotherInitialRole: qsTr("Escolha outro cargo inicial antes")
    readonly property string rosterUnsaved: qsTr("Não deu para salvar cargos, ajustes e bloqueios. Eles valem só até a sala fechar.")
    readonly property string roleRefused: qsTr("Não foi possível salvar o cargo. Use um nome que ainda não existe.")
    readonly property string roleHolders: qsTr("%1 pessoas com este cargo")
    readonly property string permissionForRole: qsTr("%1 — cargo %2")
    readonly property string permissionSkip: qsTr("Pular")
    readonly property string permissionPause: qsTr("Pausar")
    readonly property string permissionAddFromRoom: qsTr("Adicionar da sala")
    readonly property string permissionAddPlaylist: qsTr("Adicionar playlist")
    readonly property string permissionEditQueue: qsTr("Editar fila")
    readonly property string permissionUploadOwnTracks: qsTr("Enviar músicas próprias")
    readonly property string permissionAddYoutube: qsTr("Adicionar do YouTube")
    readonly property string knownTitle: qsTr("Pessoas conhecidas")
    readonly property string knownRole: qsTr("Cargo: %1")
    readonly property string blockedTag: qsTr("bloqueada")
    readonly property string unblock: qsTr("Desbloquear")
    readonly property string unblockName: qsTr("Desbloquear %1")
    readonly property string memberActions: qsTr("Cargo e permissões")
    readonly property string memberActionsName: qsTr("Cargo e permissões de %1")
    readonly property string memberMenuTitle: qsTr("%1 na sala")
    readonly property string memberRole: qsTr("Cargo de %1")
    readonly property string overrideFromRole: qsTr("Como o cargo")
    readonly property string overrideAllow: qsTr("Liberar")
    readonly property string overrideDeny: qsTr("Negar")
    readonly property string overrideName: qsTr("%1 para %2")
    readonly property string kick: qsTr("Expulsar")
    readonly property string kickName: qsTr("Expulsar %1")
    readonly property string block: qsTr("Expulsar e bloquear")
    readonly property string blockName: qsTr("Expulsar e bloquear %1")
    readonly property string closeMenu: qsTr("Fechar")

    function permissionName(index) {
        switch (index) {
        case 0:
            return permissionSkip;
        case 1:
            return permissionPause;
        case 2:
            return permissionAddFromRoom;
        case 3:
            return permissionAddPlaylist;
        case 4:
            return permissionEditQueue;
        case 5:
            return permissionUploadOwnTracks;
        default:
            return permissionAddYoutube;
        }
    }
}
