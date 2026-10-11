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

    // Changelogs that are open, by mod id. Kept here rather than in the row: the
    // ListView destroys rows that scroll out of view, which would close them.
    property var changelogOpen: ({})
    function toggleChangelog(modId) {
        const open = Object.assign({}, changelogOpen)
        if (open[modId]) delete open[modId]
        else open[modId] = true
        changelogOpen = open
    }

    // The last height of each row by mod id, and the height of a collapsed row. They size
    // a row's placeholder while the real row is not loaded (see the list). The map is
    // changed in place on purpose: nothing binds to its contents.
    property var rowHeights: ({})
    property real collapsedHeight: Kirigami.Units.gridUnit * 5
    property bool collapsedMeasured: false
    function rowHeight(modId) { return rowHeights[modId] || collapsedHeight }
    function rememberHeight(modId, height, isOpen) {
        rowHeights[modId] = height
        if (isOpen) return
        // The first closed row replaces the guess; after that only a taller one (a row
        // that wraps) raises it, so the estimate for unseen rows does not flip around.
        if (!collapsedMeasured) { collapsedMeasured = true; collapsedHeight = height }
        else if (height > collapsedHeight) collapsedHeight = height
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

    Kirigami.PromptDialog {
        id: removeDialog
        property var target: ({})
        title: qsTr("Remove %1?").arg(target.displayName)
        subtitle: qsTr("This deletes its folders from AddOns. SavedVariables under WTF are not touched.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: wam.removeAddon(target.modId)
    }

    ListView {
        id: list
        model: wam.installedAddons

        // A ListView only knows the height of the delegates it has loaded and guesses
        // the rest from their average, so one tall row (an open changelog) made the
        // scrollbar jump as rows came and went. So every row keeps a cheap placeholder
        // (cacheBuffer keeps them all), sized from the row's last known height, and the
        // real row is only loaded while it is near the viewport. Total height stays exact.
        cacheBuffer: 1000000

        delegate: Item {
            id: slot
            required property var model
            // The row's height while its real row is not loaded or has not settled yet. It
            // follows the page's guess for a row not seen so far, then the row's own height.
            property real known: page.rowHeight(model.modId)
            property bool settled: false
            readonly property real reach: list.height / 2   // half a screen on each side
            // Reads `known`, not `height`: that one depends on whether the row is loaded.
            readonly property bool near: y + known > list.contentY - reach && y < list.contentY + list.height + reach

            width: ListView.view.width
            height: loader.item && settled ? loader.item.height : known

            // A new row lays itself out over its first moments and its height wanders
            // meanwhile. The placeholder keeps its height until the row has settled, so
            // nothing below it moves.
            Timer {
                id: settle
                interval: 100
                onTriggered: {
                    if (!loader.item) return
                    slot.settled = true
                    slot.known = loader.item.height
                    page.rememberHeight(slot.model.modId, slot.known, loader.item.open)
                }
            }

            Loader {
                id: loader
                width: parent.width
                active: slot.near
                onItemChanged: {
                    slot.settled = false
                    if (item) settle.restart()
                    else settle.stop()
                }
                sourceComponent: AddonRow {
                    model: slot.model
                    open: !!page.expanded[slot.model.modId]
                    changelogOpen: !!page.changelogOpen[slot.model.modId]
                    onToggleRequested: page.toggle(slot.model.modId)
                    onChangelogToggleRequested: page.toggleChangelog(slot.model.modId)
                    onCheckRequested: {
                        wam.checkUpdate(slot.model.modId)
                        page.win.showPassiveNotification(qsTr("Checking %1…").arg(slot.model.displayName))
                    }
                    onRemoveRequested: { removeDialog.target = { modId: slot.model.modId, displayName: slot.model.displayName }; removeDialog.open() }
                    onStopTrackingRequested: wam.untrackAddon(slot.model.modId)
                    onHeightChanged: if (slot.settled) {
                        slot.known = height
                        page.rememberHeight(slot.model.modId, height, open)
                    }
                }
            }
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
