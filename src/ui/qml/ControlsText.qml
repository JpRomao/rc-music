import QtQml

QtObject {
    readonly property string close: qsTr("Fechar")
    readonly property string cancel: qsTr("Cancelar")
    readonly property string moreActions: qsTr("Mais ações")
    readonly property string moreActionsFor: qsTr("Mais ações para %1")

    // Dica de um controle: o nome e, quando existe, a descricao (motivo ou atalho).
    function toolTip(name: string, description: string): string {
        if (description.length === 0)
            return name;
        return qsTr("%1 — %2").arg(name).arg(description);
    }

    // Nome acessivel de um aviso: titulo e texto, quando existem os dois.
    function noticeName(title: string, text: string): string {
        if (title.length === 0 || text.length === 0)
            return title + text;
        return qsTr("%1. %2").arg(title).arg(text);
    }
}
