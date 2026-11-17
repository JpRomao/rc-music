import QtQuick

QtObject {
    readonly property string setupTitle: qsTr("Como você quer aparecer?")
    readonly property string setupMessage: qsTr("Escolha um apelido e um avatar. Eles aparecem para quem ouvir música junto com você. Dá para mudar depois em Configurações.")
    readonly property string nickname: qsTr("Apelido")
    readonly property string nicknameInvalid: qsTr("O apelido precisa ter de 1 a 32 caracteres, numa linha só, sem espaços repetidos nem caracteres especiais.")
    readonly property string saveFailed: qsTr("Não foi possível salvar o perfil. Tente novamente.")
    readonly property string avatar: qsTr("Avatar")
    readonly property string avatarOption: qsTr("Avatar {number}")
    readonly property string currentAvatar: qsTr("Avatar atual")
    readonly property string continueLabel: qsTr("Continuar")
    readonly property string section: qsTr("Perfil")
    readonly property string saveNickname: qsTr("Salvar apelido")
    readonly property string nicknameSaved: qsTr("Apelido salvo.")
    readonly property string customAvatar: qsTr("Usar imagem própria…")
    readonly property string chooseCustomAvatar: qsTr("Escolher imagem do avatar")
    readonly property string customAvatarFilter: qsTr("Imagens PNG ou JPEG (*.png *.jpg *.jpeg)")
    readonly property string customAvatarRejected: qsTr("Não foi possível usar essa imagem. Escolha um PNG ou JPEG de até 2 MiB.")
    readonly property string customAvatarActive: qsTr("Você está usando uma imagem própria. Escolha um avatar abaixo para trocar.")
    readonly property string unavailable: qsTr("O perfil não pode ser lido ou foi criado por uma versão mais recente do RC Music. Ele não será alterado nesta execução.")
}
