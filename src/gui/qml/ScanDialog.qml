import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Addons already sitting in AddOns/ that wam doesn't track. Rows whose .toc
// carries a CurseForge id can be adopted directly; the rest need a mod id.
// Opened from the AddOns page; scans when it opens.
Kirigami.Dialog {
    id: dialog

    signal searchRequested(string query)
    signal settingsRequested()

    title: qsTr("Existing addons")
    preferredWidth: Kirigami.Units.gridUnit * 38
    preferredHeight: Kirigami.Units.gridUnit * 28
    standardButtons: Kirigami.Dialog.Close

    onOpened: if (wam.hasWowPath) wam.scan()

    customFooterActions: [
        Kirigami.Action {
            text: qsTr("Rescan")
            icon.name: "view-refresh"
            enabled: wam.hasWowPath && !wam.scanning
            onTriggered: wam.scan()
        },
        Kirigami.Action {
            text: qsTr("Adopt all matched (%1)").arg(wam.scanResults.matchedCount)
            icon.name: "list-add"
            enabled: wam.scanResults.matchedCount > 0 && !wam.scanning
            onTriggered: wam.adoptAllMatched()
        }
    ]

    // The dialog scrolls a top-level ListView itself, so the intro text is the
    // ListView's header rather than a separate item.
    ListView {
        id: list
        model: wam.scanResults
        clip: true

        header: QQC2.Label {
            visible: wam.scanResults.count > 0
            width: list.width
            padding: Kirigami.Units.largeSpacing
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("%1 folder(s) in AddOns are not tracked by wam. Adopting records them so they can be updated; nothing on disk changes. An adopted addon's version is unknown until its first update.")
                      .arg(wam.scanResults.folderCount)
        }

        delegate: QQC2.ItemDelegate {
            id: row
            required property var modId
            required property string name
            required property string iconUrl
            required property var folders
            required property var details
            required property bool matched
            width: ListView.view.width

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                AddonIcon { source: row.iconUrl; size: Kirigami.Units.iconSizes.medium; Layout.alignment: Qt.AlignTop }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label { text: row.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    QQC2.Label {
                        text: row.details.join("\n")
                        opacity: 0.7
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }

                // Matched by .toc tag: one click.
                QQC2.Button {
                    visible: row.matched
                    text: qsTr("Adopt")
                    icon.name: "list-add"
                    onClicked: wam.adopt(row.modId, row.folders, true)
                }

                // Untagged: find the mod, or type its CurseForge id.
                RowLayout {
                    visible: !row.matched
                    QQC2.Button {
                        text: qsTr("Search")
                        icon.name: "system-search"
                        onClicked: { dialog.close(); dialog.searchRequested(row.name) }
                    }
                    QQC2.TextField {
                        id: idField
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                        placeholderText: qsTr("Mod id")
                        validator: IntValidator { bottom: 1 }
                        inputMethodHints: Qt.ImhDigitsOnly
                    }
                    QQC2.Button {
                        text: qsTr("Adopt")
                        enabled: idField.acceptableInput
                        onClicked: wam.adopt(parseInt(idField.text), row.folders, false)
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            text: wam.scanning ? qsTr("Scanning…")
                : !wam.hasWowPath ? qsTr("Set your WoW folder first")
                : qsTr("No untracked addon folders found")
            helpfulAction: Kirigami.Action {
                visible: !wam.hasWowPath
                text: qsTr("Open settings")
                onTriggered: { dialog.close(); dialog.settingsRequested() }
            }
        }
    }
}
