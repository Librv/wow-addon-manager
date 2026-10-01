import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Install window for one addon: pick the flavor, the release type and the
// version. The flavor starts as the WoW folder's flavor from Settings; changing
// it here only affects this install, and refetches the version list.
Kirigami.Dialog {
    id: dialog

    property var modId: 0
    property string modName
    property string iconUrl
    property var selectedId: 0        // file id of the chosen version, 0 = none

    signal installStarted(string name)
    signal settingsRequested()

    title: qsTr("Install %1").arg(modName)
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing
    showCloseButton: true
    standardButtons: Kirigami.Dialog.Cancel

    function openFor(id, name, icon) {
        modId = id
        modName = name
        iconUrl = icon
        selectedId = 0
        const flavors = wam.flavorsForInstall(id)
        flavorBox.model = flavors
        // The WoW folder's flavor, else the only choice, else make the user pick.
        let start = -1
        for (let i = 0; i < flavors.length; ++i) if (flavors[i].id === wam.wowFlavorId) start = i
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

    Connections {
        target: wam.installFiles
        function onChanged() { dialog.ensureSelection() }
    }

    customFooterActions: [
        Kirigami.Action {
            text: qsTr("Install")
            icon.name: "download"
            enabled: wam.hasWowPath && dialog.selectedId !== 0 && flavorBox.currentIndex >= 0
                     && !wam.installFiles.loading
            onTriggered: {
                wam.installFile(dialog.modId, dialog.selectedId, flavorBox.currentValue)
                dialog.installStarted(dialog.modName)
                dialog.close()
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

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !wam.hasWowPath
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
                model: ["release", "beta", "alpha"]
                onActivated: wam.setInstallChannel(currentText)
            }
        }

        QQC2.Label { text: qsTr("Version"); opacity: 0.7 }

        ListView {
            id: list
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

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 2
                visible: list.count === 0
                text: flavorBox.currentIndex < 0 ? qsTr("Pick a flavor to see the available versions")
                    : wam.installFiles.loading ? qsTr("Loading versions…")
                    : wam.installFiles.error.length > 0 ? qsTr("Could not load versions")
                    : qsTr("No %1 versions for this flavor").arg(channelBox.currentText)
                explanation: flavorBox.currentIndex >= 0 && !wam.installFiles.loading && wam.installFiles.error.length > 0
                             ? wam.installFiles.error : ""
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
}
