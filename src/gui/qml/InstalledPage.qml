import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    title: qsTr("Installed addons")
    readonly property var win: QQC2.ApplicationWindow.window

    actions: [
        Kirigami.Action {
            text: qsTr("Check for updates")
            icon.name: "view-refresh"
            enabled: wam.hasApiKey && !wam.checkingUpdates
            onTriggered: page.win.checkUpdates()
        },
        Kirigami.Action {
            text: qsTr("Scan for existing addons")
            icon.name: "folder-search"
            enabled: wam.hasWowPath
            onTriggered: page.win.showScan()
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

        delegate: QQC2.ItemDelegate {
            id: row
            required property var modId
            required property string displayName
            required property string fileName
            required property string iconUrl
            required property string channel
            required property bool manuallyProvided
            required property string flavorName
            required property var flavorTypeId
            width: ListView.view.width

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                AddonIcon { source: row.iconUrl; size: Kirigami.Units.iconSizes.medium }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label { text: row.displayName; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    QQC2.Label {
                        text: row.fileName + " (" + row.channel + ")" + (row.manuallyProvided ? qsTr(" [manual]") : "")
                        opacity: 0.7
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                QQC2.Label { visible: row.flavorTypeId != 0; text: row.flavorName }

                // Adopted/legacy addons have no flavor yet; assign one here.
                QQC2.ComboBox {
                    visible: row.flavorTypeId == 0
                    model: wam.flavors
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: -1
                    displayText: qsTr("Set flavor")
                    onActivated: wam.setFlavor(row.modId, currentValue)
                }

                QQC2.ToolButton {
                    icon.name: "overflow-menu"
                    onClicked: rowMenu.open()
                    QQC2.Menu {
                        id: rowMenu
                        QQC2.MenuItem {
                            text: qsTr("Remove…")
                            onTriggered: { removeDialog.target = { modId: row.modId, displayName: row.displayName }; removeDialog.open() }
                        }
                        QQC2.MenuItem { text: qsTr("Stop tracking (keep files)"); onTriggered: wam.untrackAddon(row.modId) }
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
                onTriggered: wam.hasWowPath ? page.win.showScan() : page.win.showSettings()
            }
        }
    }
}
