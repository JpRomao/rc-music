import QtQml

QtObject {
    readonly property string close: qsTr("Fechar")
    readonly property string cancel: qsTr("Cancelar")

    // Nome acessivel de um aviso: titulo e texto, quando existem os dois.
    function noticeName(title: string, text: string): string {
        if (title.length === 0 || text.length === 0)
            return title + text;
        return qsTr("%1. %2").arg(title).arg(text);
    }
}
