import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

StyledPage {
    id: page
    title: qsTr("Installed AddOns")
    readonly property var win: QQC2.ApplicationWindow.window

    // Rows whose details are open, by mod id. Several can be open at once, and
    // it lives here (not in the delegates) so scrolling does not close them.
    property var expanded: ({})
    function toggle(modId) {
        const open = Object.assign({}, expanded)
        if (open[modId]) {
            delete open[modId]
        } else {
            open[modId] = true
            wam.loadChangelog(modId)
        }
        expanded = open
    }

    actions: [
        Kirigami.Action {
            text: qsTr("Check for updates")
            icon.name: "view-refresh"
            enabled: wam.hasApiKey && !wam.checkingUpdates
            onTriggered: page.win.checkUpdates()
        },
        Kirigami.Action {
            visible: wam.pendingUpdates.count > 0
            text: qsTr("Review updates (%1)").arg(wam.pendingUpdates.count)
            icon.name: "update-none"
            onTriggered: page.win.reviewUpdates()
        },
        Kirigami.Action {
            text: qsTr("Scan for existing addons")
            icon.name: "folder-search"
            enabled: wam.hasWowPath
            onTriggered: page.win.openScan()
        }
    ]

    // Built the first time an addon is removed, not with the page.
    property var removeDialog: null
    Component {
        id: removeDialogComponent
        Kirigami.PromptDialog {
            property var target: ({})
            title: qsTr("Remove %1?").arg(target.displayName)
            subtitle: qsTr("This deletes its folders from AddOns. SavedVariables under WTF are not touched.")
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
            onAccepted: wam.removeAddon(target.modId)
        }
    }
    function askRemove(modId, displayName) {
        if (!removeDialog) removeDialog = removeDialogComponent.createObject(page)
        removeDialog.target = { modId: modId, displayName: displayName }
        removeDialog.open()
    }

    ListView {
        id: list
        model: wam.installedAddons

        delegate: AddonRow {
            open: !!page.expanded[model.modId]
            onToggleRequested: page.toggle(model.modId)
            onCheckRequested: {
                wam.checkUpdate(model.modId)
                page.win.showPassiveNotification(qsTr("Checking %1…").arg(model.displayName))
            }
            onRemoveRequested: page.askRemove(model.modId, model.displayName)
            onStopTrackingRequested: wam.untrackAddon(model.modId)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            text: wam.hasWowPath ? qsTr("No addons tracked yet") : qsTr("Set your WoW folder first")
            explanation: wam.hasWowPath ? qsTr("Already have addons installed? Scan your AddOns folder to start tracking them.") : ""
            helpfulAction: Kirigami.Action {
                text: wam.hasWowPath ? qsTr("Scan for existing addons") : qsTr("Open settings")
                icon.name: wam.hasWowPath ? "folder-search" : "configure"
                onTriggered: wam.hasWowPath ? page.win.openScan() : page.win.showSettings()
            }
        }
    }
}
