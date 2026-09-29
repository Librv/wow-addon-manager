import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: qsTr("WoW Addon Manager")
    // Wide enough for the sidebar plus a comfortable page next to it.
    width: 1180
    height: 720
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 25
    visible: true

    property var manualTarget: ({ modId: 0, fileId: 0 })

    // Pages live in a PagePool: each one is created once (in C++, via
    // createWithInitialProperties) and reused on every visit. Pushing a
    // Component or URL onto the PageRow instead makes Kirigami call
    // createObject() with a plain QtObject as parent, which is what printed
    // "Created graphical object was not placed in the graphics scene" on every
    // tab switch. Caching also keeps each page's state (search text, results).
    Kirigami.PagePool { id: mainPagePool }

    function showPage(file) {
        const page = mainPagePool.loadPage(Qt.resolvedUrl(file))
        if (pageStack.currentItem !== page) {
            pageStack.clear()
            pageStack.push(page)
        }
        return page
    }
    function showInstalled() { showPage("InstalledPage.qml") }
    function showSearch(query) {
        const page = showPage("SearchPage.qml")
        if (query) page.runQuery(query)
    }
    function showScan() { showPage("ScanPage.qml") }
    function showSettings() { showPage("SettingsPage.qml") }
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

    Component.onCompleted: showInstalled()

    globalDrawer: Kirigami.GlobalDrawer {
        isMenu: false
        // A permanent sidebar that sits beside the page instead of covering
        // and dimming it. The "Close Sidebar" button at its bottom shrinks it
        // to an icon-only strip; it never disappears completely.
        modal: false
        collapsible: true
        collapsed: false
        actions: [
            Kirigami.PagePoolAction {
                text: qsTr("Installed"); icon.name: "view-list-details"
                pagePool: mainPagePool; page: Qt.resolvedUrl("InstalledPage.qml")
            },
            Kirigami.PagePoolAction {
                text: qsTr("Search"); icon.name: "system-search"
                pagePool: mainPagePool; page: Qt.resolvedUrl("SearchPage.qml")
            },
            Kirigami.PagePoolAction {
                text: qsTr("Existing addons"); icon.name: "folder-search"
                pagePool: mainPagePool; page: Qt.resolvedUrl("ScanPage.qml")
            },
            Kirigami.PagePoolAction {
                text: qsTr("Settings"); icon.name: "configure"
                pagePool: mainPagePool; page: Qt.resolvedUrl("SettingsPage.qml")
            },
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
