import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MusicPlayer.M0

Popup {
    id: root
    objectName: "track-details-panel"
    required property ShellUiContract contract
    required property PlayerPresenter presenter
    required property Item fallbackFocusItem
    required property bool focusRestoreAllowed
    property Item restoreFocusItem: null
    property Item lastPanelFocusItem: null
    property bool diagnosticsExpanded: false
    property int focusTransferRevision: 0
    readonly property string diagnosticsText: formatDiagnostics(presenter.detailsDiagnostics)
    modal: true
    dim: false
    focus: true
    clip: true
    popupType: Popup.Item
    closePolicy: Popup.CloseOnEscape
    padding: contract.spacingUnit * 3

    TrackDetailsText {
        id: labels
    }

    function openFrom(opener) {
        if (!presenter.detailsAvailable)
            return;
        restoreFocusItem = opener;
        diagnosticsExpanded = false;
        open();
    }

    function formatInteger(value) {
        const digits = String(value);
        let grouped = "";
        for (let index = 0; index < digits.length; ++index) {
            if (index > 0 && (digits.length - index) % 3 === 0)
                grouped += labels.groupSeparator;
            grouped += digits.charAt(index);
        }
        return grouped;
    }

    function formatText(template, replacements) {
        let result = template;
        for (const key in replacements)
            result = result.replace("{" + key + "}", replacements[key]);
        return result;
    }

    function formatDuration(milliseconds) {
        const totalSeconds = Math.floor(Number(milliseconds) / 1000);
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const seconds = totalSeconds % 60;
        const twoDigits = value => value < 10 ? "0" + value : value.toString();
        return hours > 0 ? hours + ":" + twoDigits(minutes) + ":" + twoDigits(seconds) : twoDigits(minutes) + ":" + twoDigits(seconds);
    }

    function metadataFieldName(field) {
        switch (field) {
        case "title":
            return labels.title;
        case "artist":
            return labels.artist;
        case "album":
            return labels.album;
        default:
            return labels.unknownField;
        }
    }

    function metadataReadFailure(errorCode) {
        switch (errorCode) {
        case "source_unavailable":
            return labels.metadataSourceUnavailable;
        case "unsupported_format":
            return labels.metadataUnsupportedFormat;
        case "invalid_data":
            return labels.metadataInvalidData;
        default:
            return labels.metadataReadFailed;
        }
    }

    function formatDiagnostic(diagnostic) {
        switch (diagnostic.code) {
        case "metadata_field_too_large":
            return formatText(labels.metadataFieldTooLarge, {
                field: metadataFieldName(diagnostic.field),
                observed: formatInteger(diagnostic.observedBytesText),
                limit: formatInteger(diagnostic.limitBytesText)
            });
        case "metadata_total_too_large":
            return formatText(labels.metadataTotalTooLarge, {
                field: metadataFieldName(diagnostic.field),
                observed: formatInteger(diagnostic.observedBytesText),
                limit: formatInteger(diagnostic.limitBytesText)
            });
        case "artwork_encoded_bytes_exceeded":
            return formatText(labels.artworkEncodedBytesExceeded, {
                bytes: formatInteger(diagnostic.encodedBytesText)
            });
        case "artwork_axis_pixels_exceeded":
            return formatText(labels.artworkAxisPixelsExceeded, {
                width: formatInteger(diagnostic.widthPixels),
                height: formatInteger(diagnostic.heightPixels)
            });
        case "artwork_pixel_count_exceeded":
            return labels.artworkPixelCountExceeded;
        case "artwork_invalid_data":
            return labels.artworkInvalidData;
        case "metadata_read_failed":
            return metadataReadFailure(diagnostic.errorCode);
        case "metadata_read_cancelled":
            return labels.metadataReadCancelled;
        case "metadata_read_expired":
            return labels.metadataReadExpired;
        case "playback_error":
            switch (diagnostic.errorCode) {
            case "source_unavailable":
                return labels.playbackSourceUnavailable;
            case "invalid_media":
                return labels.playbackInvalidMedia;
            case "access_denied":
                return labels.playbackAccessDenied;
            default:
                return labels.playbackFailed;
            }
        default:
            return labels.unknownDiagnostic;
        }
    }

    function formatDiagnostics(diagnostics) {
        const messages = [];
        for (const diagnostic of diagnostics)
            messages.push(formatDiagnostic(diagnostic));
        return messages.join("\n");
    }

    function copyText(editor, announcement) {
        editor.selectAll();
        editor.copy();
        editor.deselect();
        content.Accessible.announce(announcement, Accessible.Polite);
    }

    function keepFocusedItemVisible(item) {
        if (!opened || !item || !item.activeFocus)
            return;
        Qt.callLater(function () {
            if (!root.opened || !item.activeFocus)
                return;
            const flickable = detailsScroll.contentItem;
            const mapped = item.mapToItem(flickable.contentItem, 0, 0);
            const margin = root.contract.spacingUnit;
            const visibleTop = flickable.contentY;
            const visibleBottom = visibleTop + flickable.height;
            let targetY = visibleTop;
            if (mapped.y - margin < visibleTop)
                targetY = mapped.y - margin;
            else if (mapped.y + item.height + margin > visibleBottom)
                targetY = mapped.y + item.height + margin - flickable.height;
            const maximumY = Math.max(0, flickable.contentHeight - flickable.height);
            flickable.contentY = Math.max(0, Math.min(maximumY, targetY));
        });
    }

    function isUsableFocusTarget(item) {
        return item && item.enabled && item.visible;
    }

    function isUsablePanelControl(item) {
        if (!isUsableFocusTarget(item))
            return false;
        if (item === closeButton)
            return true;
        if (item === pathValue || item === copyPathButton)
            return presenter.hasDetailsFilePath;
        if (item === diagnosticsToggle)
            return presenter.hasDetailsDiagnostics;
        if (item === diagnosticsValue || item === copyDiagnosticsButton)
            return presenter.hasDetailsDiagnostics && diagnosticsExpanded;
        return false;
    }

    function activePanelControl() {
        const controls = [closeButton, pathValue, copyPathButton, diagnosticsToggle, diagnosticsValue, copyDiagnosticsButton];
        for (const control of controls) {
            if (control.activeFocus && isUsablePanelControl(control))
                return control;
        }
        return null;
    }

    function reconcilePanelFocus() {
        const revision = ++focusTransferRevision;
        Qt.callLater(function () {
            if (revision !== root.focusTransferRevision || !root.opened || !root.focusRestoreAllowed)
                return;
            const current = root.activePanelControl();
            if (current) {
                root.keepFocusedItemVisible(current);
                return;
            }
            const target = root.isUsablePanelControl(root.lastPanelFocusItem) ? root.lastPanelFocusItem : closeButton;
            if (root.isUsablePanelControl(target))
                target.forceActiveFocus(Qt.TabFocusReason);
        });
    }

    function observePanelFocus(item) {
        if (item.activeFocus) {
            lastPanelFocusItem = item;
            keepFocusedItemVisible(item);
        } else if (opened) {
            reconcilePanelFocus();
        }
    }

    function lastFocusItem() {
        if (presenter.hasDetailsDiagnostics)
            return diagnosticsExpanded ? copyDiagnosticsButton : diagnosticsToggle;
        if (presenter.hasDetailsFilePath)
            return copyPathButton;
        return closeButton;
    }

    onFocusRestoreAllowedChanged: {
        if (focusRestoreAllowed && opened)
            reconcilePanelFocus();
    }
    onOpened: {
        ++focusTransferRevision;
        lastPanelFocusItem = closeButton;
        closeButton.forceActiveFocus(Qt.TabFocusReason);
        content.Accessible.announce(labels.opened, Accessible.Polite);
    }
    onClosed: {
        diagnosticsExpanded = false;
        const target = restoreFocusItem;
        restoreFocusItem = null;
        lastPanelFocusItem = null;
        const revision = ++focusTransferRevision;
        if (focusRestoreAllowed)
            Qt.callLater(function () {
                if (revision !== root.focusTransferRevision || root.opened || !root.focusRestoreAllowed)
                    return;
                const destination = root.isUsableFocusTarget(target) ? target : root.fallbackFocusItem;
                if (root.isUsableFocusTarget(destination))
                    destination.forceActiveFocus(Qt.TabFocusReason);
            });
    }

    background: Rectangle {
        color: root.contract.surfaceRaisedColor
        radius: root.contract.radiusMedium
        border.width: root.contract.highContrast ? 2 : 1
        border.color: root.contract.highContrast ? root.contract.focusColor : root.contract.borderColor
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: root.contract.panelTransitionMs
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: root.contract.panelTransitionMs
        }
    }

    contentItem: ColumnLayout {
        id: content
        spacing: root.contract.spacingUnit * 2
        Accessible.role: Accessible.Dialog
        Accessible.name: root.contract.detailsTitle

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.contract.detailsTitle
                textFormat: Text.PlainText
                color: root.contract.textPrimaryColor
                font.pixelSize: 20
                font.bold: true
                Accessible.ignored: true
            }
            AccessibleTransportButton {
                id: closeButton
                objectName: "track-details-close"
                contract: root.contract
                text: "✕"
                accessibleName: labels.close
                KeyNavigation.tab: root.presenter.hasDetailsFilePath ? pathValue : (root.presenter.hasDetailsDiagnostics ? diagnosticsToggle : closeButton)
                KeyNavigation.backtab: root.lastFocusItem()
                onActiveFocusChanged: root.observePanelFocus(closeButton)
                onClicked: root.close()
            }
        }

        ScrollView {
            id: detailsScroll
            objectName: "track-details-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: detailsScroll.availableWidth
                spacing: root.contract.spacingUnit

                Text {
                    text: labels.trackSection
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    font.bold: true
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-title"
                    label: labels.title
                    valueKnown: root.presenter.hasDetailsTitle
                    valueText: root.presenter.detailsTitle
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-artist"
                    label: labels.artist
                    valueKnown: root.presenter.hasDetailsArtist
                    valueText: root.presenter.detailsArtist
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-album"
                    label: labels.album
                    valueKnown: root.presenter.hasDetailsAlbum
                    valueText: root.presenter.detailsAlbum
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-track-number"
                    label: labels.trackNumber
                    valueKnown: root.presenter.hasDetailsTrackNumber
                    valueText: root.presenter.hasDetailsTrackNumber ? root.formatInteger(root.presenter.detailsTrackNumber) : ""
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-year"
                    label: labels.year
                    valueKnown: root.presenter.hasDetailsYear
                    valueText: root.presenter.hasDetailsYear ? Number(root.presenter.detailsYear).toString() : ""
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-duration"
                    label: labels.duration
                    valueKnown: root.presenter.hasDetailsDuration
                    valueText: root.presenter.hasDetailsDuration ? root.formatDuration(root.presenter.detailsDurationMs) : ""
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit
                    implicitHeight: 1
                    color: root.contract.borderColor
                    Accessible.ignored: true
                }
                Text {
                    text: labels.fileSection
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    font.bold: true
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-file-name"
                    label: labels.fileName
                    valueKnown: root.presenter.hasDetailsFileName
                    valueText: root.presenter.detailsFileName
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-file-size"
                    label: labels.fileSize
                    valueKnown: root.presenter.hasDetailsFileSize
                    valueText: root.presenter.hasDetailsFileSize ? root.formatText(labels.bytesValue, {
                        value: root.formatInteger(root.presenter.detailsFileSizeBytesText)
                    }) : ""
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-modified"
                    label: labels.modifiedUtc
                    valueKnown: root.presenter.hasDetailsModifiedUtc
                    valueText: root.presenter.detailsModifiedUtc
                }
                Text {
                    text: labels.filePath
                    textFormat: Text.PlainText
                    color: root.contract.textSecondaryColor
                    Accessible.ignored: true
                }
                TextArea {
                    id: pathValue
                    objectName: "details-file-path-value"
                    Layout.fillWidth: true
                    Layout.minimumHeight: root.contract.controlTargetMinimum
                    text: root.presenter.hasDetailsFilePath ? root.presenter.detailsFilePath : root.contract.notInformed
                    textFormat: Text.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    activeFocusOnTab: root.presenter.hasDetailsFilePath
                    color: root.contract.textPrimaryColor
                    palette.highlight: root.contract.accentColor
                    palette.highlightedText: root.contract.accentTextColor
                    Accessible.id: objectName
                    Accessible.name: labels.filePath + ": " + text
                    KeyNavigation.tab: copyPathButton
                    KeyNavigation.backtab: closeButton
                    onActiveFocusChanged: root.observePanelFocus(pathValue)
                    background: Rectangle {
                        color: root.contract.surfaceColor
                        radius: root.contract.radiusSmall
                        border.width: pathValue.activeFocus ? 3 : 1
                        border.color: pathValue.activeFocus ? root.contract.focusColor : root.contract.borderColor
                    }
                }
                AccessibleTransportButton {
                    id: copyPathButton
                    objectName: "details-copy-path"
                    contract: root.contract
                    text: labels.copyPath
                    accessibleName: labels.copyPath
                    enabled: root.presenter.hasDetailsFilePath
                    Layout.alignment: Qt.AlignRight
                    KeyNavigation.tab: root.presenter.hasDetailsDiagnostics ? diagnosticsToggle : closeButton
                    KeyNavigation.backtab: pathValue
                    onActiveFocusChanged: root.observePanelFocus(copyPathButton)
                    onClicked: root.copyText(pathValue, labels.pathCopied)
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit
                    implicitHeight: 1
                    color: root.contract.borderColor
                    Accessible.ignored: true
                }
                Text {
                    text: labels.audioSection
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    font.bold: true
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-container"
                    label: labels.container
                    valueKnown: root.presenter.hasDetailsContainer
                    valueText: root.presenter.detailsContainer
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-codec"
                    label: labels.codec
                    valueKnown: root.presenter.hasDetailsCodec
                    valueText: root.presenter.detailsCodec
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-bitrate"
                    label: labels.bitrate
                    valueKnown: root.presenter.hasDetailsBitrate
                    valueText: root.presenter.hasDetailsBitrate ? root.formatText(labels.kilobitsPerSecondValue, {
                        value: root.formatInteger(Math.round(Number(root.presenter.detailsBitrateBps) / 1000))
                    }) : ""
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-sample-rate"
                    label: labels.sampleRate
                    valueKnown: root.presenter.hasDetailsSampleRate
                    valueText: root.presenter.hasDetailsSampleRate ? root.formatText(labels.hertzValue, {
                        value: root.formatInteger(root.presenter.detailsSampleRateHz)
                    }) : ""
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-channels"
                    label: labels.channels
                    valueKnown: root.presenter.hasDetailsChannels
                    valueText: root.presenter.hasDetailsChannels ? root.formatInteger(root.presenter.detailsChannels) : ""
                }
                TrackDetailsField {
                    Layout.fillWidth: true
                    contract: root.contract
                    controlId: "details-bit-depth"
                    label: labels.bitDepth
                    valueKnown: root.presenter.hasDetailsBitDepth
                    valueText: root.presenter.hasDetailsBitDepth ? root.formatText(labels.bitsValue, {
                        value: root.formatInteger(root.presenter.detailsBitDepth)
                    }) : ""
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: root.contract.spacingUnit
                    implicitHeight: 1
                    color: root.contract.borderColor
                    Accessible.ignored: true
                }
                Text {
                    text: labels.diagnosticsSection
                    textFormat: Text.PlainText
                    color: root.contract.textPrimaryColor
                    font.bold: true
                }
                Text {
                    objectName: "details-diagnostics-empty"
                    Layout.fillWidth: true
                    visible: !root.presenter.hasDetailsDiagnostics
                    text: labels.noDiagnostics
                    textFormat: Text.PlainText
                    color: root.contract.textSecondaryColor
                    Accessible.id: objectName
                    Accessible.name: text
                }
                AccessibleTransportButton {
                    id: diagnosticsToggle
                    objectName: "details-diagnostics-toggle"
                    contract: root.contract
                    text: root.diagnosticsExpanded ? labels.hideDiagnostics : labels.showDiagnostics
                    accessibleName: text
                    visible: root.presenter.hasDetailsDiagnostics
                    enabled: visible
                    checkable: true
                    checked: root.diagnosticsExpanded
                    Accessible.checkable: true
                    Accessible.checked: checked
                    KeyNavigation.tab: root.diagnosticsExpanded ? diagnosticsValue : closeButton
                    KeyNavigation.backtab: root.presenter.hasDetailsFilePath ? copyPathButton : closeButton
                    onActiveFocusChanged: root.observePanelFocus(diagnosticsToggle)
                    onClicked: root.diagnosticsExpanded = !root.diagnosticsExpanded
                }
                TextArea {
                    id: diagnosticsValue
                    objectName: "details-diagnostics-value"
                    Layout.fillWidth: true
                    Layout.minimumHeight: visible ? root.contract.controlTargetMinimum * 2 : 0
                    visible: root.presenter.hasDetailsDiagnostics && root.diagnosticsExpanded
                    text: root.diagnosticsText
                    textFormat: Text.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    activeFocusOnTab: visible
                    color: root.contract.textPrimaryColor
                    palette.highlight: root.contract.accentColor
                    palette.highlightedText: root.contract.accentTextColor
                    Accessible.id: objectName
                    Accessible.name: labels.diagnosticsSection + ": " + text
                    KeyNavigation.tab: copyDiagnosticsButton
                    KeyNavigation.backtab: diagnosticsToggle
                    onActiveFocusChanged: root.observePanelFocus(diagnosticsValue)
                    background: Rectangle {
                        color: root.contract.surfaceColor
                        radius: root.contract.radiusSmall
                        border.width: diagnosticsValue.activeFocus ? 3 : 1
                        border.color: diagnosticsValue.activeFocus ? root.contract.focusColor : root.contract.borderColor
                    }
                }
                AccessibleTransportButton {
                    id: copyDiagnosticsButton
                    objectName: "details-copy-diagnostics"
                    contract: root.contract
                    text: labels.copyDiagnostics
                    accessibleName: labels.copyDiagnostics
                    visible: root.presenter.hasDetailsDiagnostics && root.diagnosticsExpanded
                    enabled: visible
                    Layout.alignment: Qt.AlignRight
                    KeyNavigation.tab: closeButton
                    KeyNavigation.backtab: diagnosticsValue
                    onActiveFocusChanged: root.observePanelFocus(copyDiagnosticsButton)
                    onClicked: root.copyText(diagnosticsValue, labels.diagnosticsCopied)
                }
            }
        }
    }
}
