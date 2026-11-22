import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

// Separador de menu: linha de 1 px com 6 px de margem vertical.
MenuSeparator {
    id: separator

    required property ShellUiContract contract

    leftPadding: 6
    rightPadding: 6
    topPadding: 6
    bottomPadding: 6

    contentItem: Rectangle {
        implicitHeight: 1
        color: separator.contract.dividerColor
    }
}
