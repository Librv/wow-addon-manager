import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Install window for one addon: pick the flavor, the release type and the
// version. The flavor starts as the WoW folder's flavor from Settings (or the
// addon's own); changing it here only affects this addon, and refetches the
// version list.
//
// In link mode the same window is used to tell wam which CurseForge file an
// adopted addon matches: the button says "Link" and nothing is downloaded.
Kirigami.Dialog {
    id: dialog

    property var modId: 0
    property string modName
    property string iconUrl
    property var selectedId: 0        // file id of the chosen version, 0 = none
    property bool linkMode: false
    readonly property bool hasVersions: wam.installFiles.count > 0
    // Loaded, and there is nothing to install (or it failed): worth showing in red.
    readonly property bool problem: flavorBox.currentIndex >= 0 && !wam.installFiles.loading && !hasVersions
    // Installing a flavor other than the WoW folder's (only when that flavor is known).
    // Release types in dropdown order; the labels add how many versions each has among those loaded.
    readonly property var channelNames: ["release", "beta", "alpha"]
    readonly property var channelLabels: channelNames.map(n => n + " (" + wam.installFiles.channelCounts[n]
                                                          + (wam.installFiles.partial ? "+" : "") + ")")
    // The other release types that have versions, e.g. "beta (8), alpha (2)"; empty if none.
    readonly property string otherChannelsNote: channelNames
        .filter(n => n !== wam.installFiles.channel && wam.installFiles.channelCounts[n] > 0)
        .map(n => n + " (" + wam.installFiles.channelCounts[n] + (wam.installFiles.partial ? "+" : "") + ")")
        .join(", ")
    readonly property bool flavorMismatch: !linkMode && flavorBox.currentIndex >= 0
                                           && wam.wowFlavorId !== 0 && flavorBox.currentValue !== wam.wowFlavorId

    signal installStarted(string name)
    signal settingsRequested()

    title: linkMode ? qsTr("Link %1 to CurseForge").arg(modName) : qsTr("Install %1").arg(modName)
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing
    showCloseButton: true
    standardButtons: Kirigami.Dialog.Cancel

    // preferredFlavorId: the flavor to start on (0 = the WoW folder's).
    function openFor(id, name, icon, preferredFlavorId, link) {
        modId = id
        modName = name
        iconUrl = icon
        linkMode = !!link
        selectedId = 0
        const flavors = wam.flavorsForInstall(id)
        flavorBox.model = flavors
        // The wanted flavor, else the only choice, else make the user pick.
        const want = preferredFlavorId > 0 ? preferredFlavorId : wam.wowFlavorId
        let start = -1
        for (let i = 0; i < flavors.length; ++i) if (flavors[i].id === want) start = i
        if (start < 0 && flavors.length === 1) start = 0
        flavorBox.currentIndex = start
        channelBox.currentIndex = 0
        wam.setInstallChannel("release")
        reload()
        open()
    }

    function reload() {
        selectedId = 0
        if (flavorBox.currentIndex < 0) wam.clearInstallFiles()
        else wam.loadInstallFiles(modId, flavorBox.currentValue)
    }

    // Keep the chosen version while it is still listed; otherwise take the newest.
    function ensureSelection() {
        const files = wam.installFiles
        for (let i = 0; i < files.count; ++i)
            if (files.fileIdAt(i) === selectedId) return
        selectedId = files.count > 0 ? files.fileIdAt(0) : 0
    }

    // The footer action's work, run directly or after the flavor confirmation.
    function performAction() {
        if (dialog.linkMode) {
            wam.linkFile(dialog.modId, dialog.selectedId, flavorBox.currentValue)
        } else {
            wam.installFile(dialog.modId, dialog.selectedId, flavorBox.currentValue)
            dialog.installStarted(dialog.modName)
        }
        dialog.close()
    }

    Connections {
        target: wam.installFiles
        function onChanged() { dialog.ensureSelection() }
    }

    customFooterActions: [
        Kirigami.Action {
            text: dialog.linkMode ? qsTr("Link") : qsTr("Install")
            icon.name: dialog.linkMode ? "insert-link" : "download"
            // Linking records a version; it needs neither the WoW folder nor a download.
            enabled: (dialog.linkMode || wam.hasWowPath) && dialog.selectedId !== 0 && flavorBox.currentIndex >= 0
                     && !wam.installFiles.loading
            onTriggered: {
                if (dialog.flavorMismatch) flavorConfirm.open()
                else dialog.performAction()
            }
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            spacing: Kirigami.Units.largeSpacing
            AddonIcon { source: dialog.iconUrl; size: Kirigami.Units.iconSizes.large }
            Kirigami.Heading { level: 2; text: dialog.modName; elide: Text.ElideRight; Layout.fillWidth: true }
        }

        FieldNote {
            Layout.fillWidth: true
            visible: dialog.linkMode
            text: qsTr("Pick the version that matches what is installed. Nothing is downloaded and nothing in your AddOns folder changes. The addon's flavor is taken from the flavor you pick here.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !dialog.linkMode && !wam.hasWowPath
            type: Kirigami.MessageType.Warning
            text: qsTr("Set your WoW folder in Settings before installing.")
            actions: Kirigami.Action {
                text: qsTr("Open settings")
                onTriggered: { dialog.close(); dialog.settingsRequested() }
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            QQC2.Label { text: qsTr("Flavor"); opacity: 0.7 }
            QQC2.ComboBox {
                id: flavorBox
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                displayText: currentIndex < 0 ? qsTr("Pick a flavor") : currentText
                onActivated: dialog.reload()
            }

            QQC2.Label { text: qsTr("Release type"); opacity: 0.7 }
            QQC2.ComboBox {
                id: channelBox
                Layout.fillWidth: true
                model: dialog.channelLabels
                onActivated: wam.setInstallChannel(dialog.channelNames[currentIndex])
            }
        }

        QQC2.Label { text: qsTr("Version"); opacity: 0.7; visible: dialog.hasVersions }

        ListView {
            id: list
            visible: dialog.hasVersions
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 16)
            clip: true
            model: wam.installFiles

            delegate: QQC2.RadioDelegate {
                id: row
                required property var fileId
                required property string displayName
                required property string channel
                required property string gameVersions
                required property string date
                required property bool blocked
                width: ListView.view.width
                checked: row.fileId === dialog.selectedId
                onClicked: dialog.selectedId = row.fileId

                contentItem: ColumnLayout {
                    spacing: 0
                    QQC2.Label {
                        text: row.displayName
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        leftPadding: row.indicator.width + row.spacing
                    }
                    QQC2.Label {
                        text: [row.channel, row.gameVersions, row.date].filter(s => s.length > 0).join(" · ")
                              + (row.blocked ? qsTr(" · download blocked by the author") : "")
                        opacity: 0.7
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        leftPadding: row.indicator.width + row.spacing
                    }
                }
            }
        }

        // Shown instead of the list (and its "Version" heading) when there is nothing to pick:
        // grey while it is still loading or waiting for a flavor, red when there is nothing to install.
        QQC2.Label {
            Layout.fillWidth: true
            visible: !dialog.hasVersions
            wrapMode: Text.WordWrap
            color: dialog.problem ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
            opacity: dialog.problem ? 1 : 0.7
            text: {
                if (flavorBox.currentIndex < 0) return qsTr("Pick a flavor to see the available versions.")
                if (wam.installFiles.loading) return qsTr("Loading versions…")
                if (wam.installFiles.error.length > 0) return qsTr("Could not load versions: %1").arg(wam.installFiles.error)
                const base = wam.installFiles.hasMore
                    ? qsTr("No %1 versions found so far for %2.").arg(wam.installFiles.channel).arg(flavorBox.currentText)
                    : qsTr("No %1 versions for %2.").arg(wam.installFiles.channel).arg(flavorBox.currentText)
                return dialog.otherChannelsNote.length > 0
                    ? base + " " + qsTr("Other release types have versions: %1.").arg(dialog.otherChannelsNote)
                    : base
            }
        }

        // Fetches more once everything already loaded is on screen.
        QQC2.Button {
            flat: true
            Layout.alignment: Qt.AlignHCenter
            visible: wam.installFiles.hasMore
            enabled: !wam.installFiles.loading
            text: wam.installFiles.loading ? qsTr("Loading…") : qsTr("Show more versions")
            onClicked: wam.showMoreInstallFiles()
        }
    }

    // Shown when the chosen flavor differs from the WoW folder's flavor in Settings.
    Kirigami.PromptDialog {
        id: flavorConfirm
        title: qsTr("Install for a different flavor?")
        subtitle: qsTr("Your WoW folder is set to %1, but this addon is being installed for %2. It may not work in the other flavor.")
                      .arg(wam.wowFlavorName).arg(flavorBox.currentText)
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: dialog.performAction()
    }
}
