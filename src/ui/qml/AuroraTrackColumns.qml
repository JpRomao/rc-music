import QtQml

// The column measures of a track list: the row and the header above it read the same numbers, so they line up.
// Side padding 10, number 44, title 1.4 fr, album 1 fr (none when compact), duration 56 and the 96 of the actions.
QtObject {
    property real width: 0
    property bool compact: false

    readonly property int spacing: 12
    readonly property int sidePadding: 10
    readonly property int numberWidth: 44
    readonly property int durationWidth: 56
    readonly property int actionsWidth: 96
    // Free width shared by the title column and the album column.
    readonly property real flexibleWidth: width - 2 * sidePadding - numberWidth - durationWidth - actionsWidth - spacing * (compact ? 3 : 4)
    readonly property real titleWidth: compact ? flexibleWidth : flexibleWidth * 1.4 / 2.4
    readonly property real albumWidth: compact ? 0 : flexibleWidth / 2.4
    readonly property real titleX: sidePadding + numberWidth + spacing
    readonly property real albumX: titleX + titleWidth + spacing
    readonly property real durationX: compact ? albumX : albumX + albumWidth + spacing
    readonly property real actionsX: durationX + durationWidth + spacing
}
