import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: qsTr("WoW Addon Manager")
    width: 980
    height: 660
    visible: true

    property var manualTarget: ({ modId: 0, fileId: 0 })

    function show(page) { pageStack.clear(); pageStack.push(page) }
    function showInstalled() { show(installedPage) }
    function showSearch(query) { pageStack.clear(); pageStack.push(searchPage, { initialQuery: query || "" }) }
    function showScan() { show(scanPage) }
    function showSettings() { show(settingsPage) }
    function checkUpdates() {
        if (!wam.checkingUpdates) {
            wam.checkAllUpdates()
            showPassiveNotification(qsTr("Checking for updates…"))
        }
    }
    function pickZip(modId, fileId) {
        manualTarget = { modId: modId, fileId: fileId }
        zipDialog.open()
    }

    Component { id: installedPage; InstalledPage {} }
    Component { id: searchPage;    SearchPage {} }
    Component { id: scanPage;      ScanPage {} }
    Component { id: settingsPage;  SettingsPage {} }

    pageStack.initialPage: installedPage

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: false
        actions: [
            Kirigami.Action { text: qsTr("Installed"); icon.name: "view-list-details"; onTriggered: root.showInstalled() },
            Kirigami.Action { text: qsTr("Search"); icon.name: "system-search"; onTriggered: root.showSearch() },
            Kirigami.Action { text: qsTr("Existing addons"); icon.name: "folder-search"; onTriggered: root.showScan() },
            Kirigami.Action { text: qsTr("Settings"); icon.name: "configure"; onTriggered: root.showSettings() },
            Kirigami.Action { separator: true },
            Kirigami.Action {
                text: qsTr("Check for updates")
                icon.name: "view-refresh"
                enabled: wam.hasApiKey && !wam.checkingUpdates
                onTriggered: root.checkUpdates()
            },
            Kirigami.Action {
                visible: wam.pendingUpdates.count > 0
                text: qsTr("Review updates (%1)").arg(wam.pendingUpdates.count)
                icon.name: "update-none"
                onTriggered: updatesDialog.open()
            }
        ]
    }

    Connections {
        target: wam
        function onErrorOccurred(context, message) { root.showPassiveNotification(context + ": " + message, "long") }
        function onInstallFinished(modId, name) { root.showPassiveNotification(qsTr("Installed %1").arg(name)) }
        function onAdopted(modId, name, folderCount) {
            root.showPassiveNotification(qsTr("Adopted %1 (%2 folder(s))").arg(name).arg(folderCount))
        }
        function onDownloadBlocked(modId, modName, modSlug, fileId, fileName) {
            blockedDialog.info = { modId: modId, modName: modName, modSlug: modSlug, fileId: fileId, fileName: fileName }
            blockedDialog.open()
        }
        function onUpdateCheckFinished(available) {
            if (available > 0) updatesDialog.open()
            else root.showPassiveNotification(qsTr("All addons are up to date"))
        }
    }

    UpdatesDialog {
        id: updatesDialog
        onPickZipRequested: (modId, fileId) => root.pickZip(modId, fileId)
    }

    FileDialog {
        id: zipDialog
        title: qsTr("Select the downloaded addon zip")
        nameFilters: [qsTr("Zip archives (*.zip)")]
        onAccepted: wam.installManual(root.manualTarget.modId, root.manualTarget.fileId, wam.localPath(selectedFile))
    }

    Kirigami.Dialog {
        id: blockedDialog
        property var info: ({})
        title: qsTr("Download blocked by author")
        standardButtons: Kirigami.Dialog.Close

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.WordWrap
                text: qsTr("The author of %1 does not allow third-party downloads. Download %2 from CurseForge yourself, then select the zip here.")
                          .arg(blockedDialog.info.modName).arg(blockedDialog.info.fileName)
            }
            RowLayout {
                QQC2.Button {
                    text: qsTr("Open on CurseForge")
                    enabled: !!blockedDialog.info.modSlug
                    onClicked: Qt.openUrlExternally("https://www.curseforge.com/wow/addons/" + blockedDialog.info.modSlug)
                }
                QQC2.Button {
                    text: qsTr("Choose downloaded zip…")
                    onClicked: { blockedDialog.close(); root.pickZip(blockedDialog.info.modId, blockedDialog.info.fileId) }
                }
            }
        }
    }
}
