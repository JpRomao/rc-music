import QtQml

QtObject {
    readonly property string backwardTemplate: qsTr("Voltar %1")
    readonly property string forwardTemplate: qsTr("Avançar %1")

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
