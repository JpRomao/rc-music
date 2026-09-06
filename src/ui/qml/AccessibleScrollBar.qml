import QtQuick
import QtQuick.Controls
import MusicPlayer.M0

ScrollBar {
    id: control
    required property ShellUiContract contract
    activeFocusOnTab: false

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: width / 2
        color: control.contract.highContrast ? control.contract.textPrimaryColor : (control.pressed ? control.contract.accentColor : control.contract.borderColor)
        opacity: control.contract.highContrast ? 1 : (control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1) ? 1 : 0)
        Behavior on opacity {
            enabled: !control.contract.highContrast
            NumberAnimation {
                duration: control.contract.feedbackTransitionMs
            }
        }
    }
    background: Rectangle {
        visible: control.contract.highContrast
        color: control.contract.surfaceColor
        border.width: 1
        border.color: control.contract.textPrimaryColor
    }
}
