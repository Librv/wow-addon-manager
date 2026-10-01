import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
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

        delegate: Item {
            id: row
            required property var modId
            required property string displayName
            required property string iconUrl
            required property string description
            required property string versionText
            required property string channelText
            required property string flavorName
            required property var flavorTypeId
            required property string releasedText
            required property string installedText
            required property string sourceText
            required property var folders
            required property bool linked
            required property string modSlug
            required property string changelogState
            required property string changelogText

            readonly property bool open: !!page.expanded[row.modId]
            property bool showFullChangelog: false

            width: ListView.view.width
            height: col.implicitHeight

            ColumnLayout {
                id: col
                width: parent.width
                spacing: 0

                // Same size as a Search result: a large icon, the name, and a line of details.
                QQC2.ItemDelegate {
                    Layout.fillWidth: true
                    onClicked: page.toggle(row.modId)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        AddonIcon { source: row.iconUrl; size: Kirigami.Units.iconSizes.large }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            QQC2.Label { text: row.displayName; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            QQC2.Label { text: row.description; opacity: 0.7; elide: Text.ElideRight; Layout.fillWidth: true }
                        }

                        QQC2.ToolButton {
                            icon.name: "view-refresh"
                            enabled: wam.hasApiKey
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.text: qsTr("Check %1 for updates").arg(row.displayName)
                            onClicked: {
                                wam.checkUpdate(row.modId)
                                page.win.showPassiveNotification(qsTr("Checking %1…").arg(row.displayName))
                            }
                        }
                        QQC2.ToolButton {
                            icon.name: row.open ? "arrow-up" : "arrow-down"
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.text: row.open ? qsTr("Hide details") : qsTr("Show details")
                            onClicked: page.toggle(row.modId)
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

                // ---- details
                ColumnLayout {
                    visible: row.open
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.iconSizes.large + Kirigami.Units.largeSpacing * 2
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    GridLayout {
                        columns: 2
                        columnSpacing: Kirigami.Units.largeSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        QQC2.Label { text: qsTr("Version"); opacity: 0.7 }
                        QQC2.Label { text: row.versionText; opacity: row.linked ? 1 : 0.7 }

                        QQC2.Label { text: qsTr("Release channel"); opacity: 0.7 }
                        QQC2.Label { text: row.channelText }

                        QQC2.Label { text: qsTr("Flavor"); opacity: 0.7 }
                        RowLayout {
                            QQC2.Label { visible: row.flavorTypeId != 0; text: row.flavorName }
                            // Only while the flavor is unknown: linking or updating an addon fills it in.
                            QQC2.ComboBox {
                                visible: row.flavorTypeId == 0
                                enabled: wam.flavors.length > 0
                                model: wam.flavors
                                textRole: "name"
                                valueRole: "id"
                                currentIndex: -1
                                displayText: qsTr("Set flavor")
                                onActivated: wam.setFlavor(row.modId, currentValue)
                            }
                        }

                        QQC2.Label { text: qsTr("Released"); opacity: 0.7; visible: row.releasedText.length > 0 }
                        QQC2.Label { text: row.releasedText; visible: row.releasedText.length > 0 }

                        QQC2.Label { text: qsTr("Installed"); opacity: 0.7 }
                        QQC2.Label { text: row.installedText }

                        QQC2.Label { text: qsTr("Source"); opacity: 0.7 }
                        QQC2.Label { text: row.sourceText }
                    }

                    QQC2.Label { text: qsTr("Folders (%1)").arg(row.folders.length); opacity: 0.7 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Repeater {
                            model: row.folders
                            delegate: Rectangle {
                                id: chip
                                required property string modelData
                                implicitWidth: chipLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                                implicitHeight: chipLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                                radius: Kirigami.Units.smallSpacing
                                color: Kirigami.Theme.alternateBackgroundColor
                                QQC2.Label {
                                    id: chipLabel
                                    anchors.centerIn: parent
                                    text: chip.modelData
                                    font.family: "monospace"
                                }
                            }
                        }
                    }

                    // The changelog of the installed version, as plain text.
                    ColumnLayout {
                        visible: row.linked
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Label { text: qsTr("What's new in this version"); opacity: 0.7 }
                        QQC2.Label {
                            id: changelog
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            maximumLineCount: row.showFullChangelog ? 100000 : 6
                            text: row.changelogState === "ready"
                                    ? (row.changelogText.length > 0 ? row.changelogText : qsTr("No changelog was provided for this version."))
                                : row.changelogState === "failed" ? qsTr("Could not load the changelog: %1").arg(row.changelogText)
                                : qsTr("Loading…")
                        }
                        QQC2.Button {
                            flat: true
                            visible: changelog.truncated || row.showFullChangelog
                            text: row.showFullChangelog ? qsTr("Show less") : qsTr("Show more")
                            onClicked: row.showFullChangelog = !row.showFullChangelog
                        }
                    }

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        // Only once there is a CurseForge file to point at: adopted addons get these after linking or updating.
                        QQC2.Button {
                            visible: row.linked && row.modSlug.length > 0
                            icon.name: "internet-services"
                            text: qsTr("View on CurseForge")
                            onClicked: Qt.openUrlExternally(wam.modPageUrl(row.modSlug))
                        }
                        QQC2.Button {
                            visible: !row.linked
                            enabled: wam.hasApiKey
                            icon.name: "insert-link"
                            text: qsTr("Link to CurseForge…")
                            onClicked: page.win.openLink(row.modId, row.displayName, row.iconUrl, row.flavorTypeId)
                        }
                        QQC2.Button {
                            visible: row.linked
                            enabled: wam.hasApiKey
                            icon.name: "download"
                            text: qsTr("Install another version…")
                            onClicked: page.win.openInstall(row.modId, row.displayName, row.iconUrl, row.flavorTypeId)
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }
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
