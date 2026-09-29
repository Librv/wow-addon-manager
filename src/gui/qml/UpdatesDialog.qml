import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Update review popup. Shows one addon's diff at a time (the head of the
// queue). Apply installs it and moves on to the next diff; Apply all accepts
// every remaining update; Skip drops one without installing it.
Kirigami.Dialog {
    id: dialog

    // Always a full map (empty strings when the queue is empty), never undefined.
    readonly property var u: wam.pendingUpdates.head
    readonly property int pending: wam.pendingUpdates.count

    signal pickZipRequested(var modId, var fileId)

    title: qsTr("Updates available")
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing
    showCloseButton: true
    standardButtons: Kirigami.Dialog.NoButton

    // Nothing left to review: get out of the way.
    onPendingChanged: if (pending === 0 && opened) close()

    customFooterActions: [
        Kirigami.Action {
            text: qsTr("Skip")
            icon.name: "go-next"
            enabled: dialog.pending > 0 && dialog.u.status !== "applying"
            onTriggered: wam.skipUpdate(dialog.u.modId)
        },
        Kirigami.Action {
            text: qsTr("Apply")
            icon.name: "dialog-ok-apply"
            enabled: dialog.pending > 0 && dialog.u.blocked !== true && dialog.u.status !== "applying"
            onTriggered: wam.applyUpdate(dialog.u.modId)
        },
        Kirigami.Action {
            text: qsTr("Apply all (%1)").arg(wam.pendingUpdates.applicableCount)
            icon.name: "dialog-ok"
            enabled: wam.pendingUpdates.applicableCount > 0
            onTriggered: wam.applyAllUpdates()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            text: qsTr("%1 pending").arg(dialog.pending)
            opacity: 0.7
        }

        RowLayout {
            spacing: Kirigami.Units.largeSpacing
            AddonIcon { source: dialog.u.iconUrl; size: Kirigami.Units.iconSizes.huge }
            Kirigami.Heading {
                level: 2
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dialog.u.modName + (dialog.u.flavorName ? "  [" + dialog.u.flavorName + "]" : "")
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            QQC2.Label { text: qsTr("Installed"); opacity: 0.7 }
            QQC2.Label { text: dialog.u.currentFile; elide: Text.ElideMiddle; Layout.fillWidth: true }
            QQC2.Label { text: qsTr("Latest"); opacity: 0.7 }
            QQC2.Label { text: dialog.u.latestFile; font.bold: true; elide: Text.ElideMiddle; Layout.fillWidth: true }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.u.blocked === true
            type: Kirigami.MessageType.Warning
            text: qsTr("The author blocked third-party downloads. Download the file from CurseForge, then select the zip.")
            actions: [
                Kirigami.Action {
                    text: qsTr("Open on CurseForge")
                    onTriggered: Qt.openUrlExternally("https://www.curseforge.com/wow/addons/" + dialog.u.modSlug)
                },
                Kirigami.Action {
                    text: qsTr("Choose zip…")
                    onTriggered: dialog.pickZipRequested(dialog.u.modId, dialog.u.latestFileId)
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.u.status === "failed"
            type: Kirigami.MessageType.Error
            text: dialog.u.error
        }
    }
}
