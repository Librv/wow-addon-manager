import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The expandable details under an installed addon's row: facts, folders,
// changelog and the action buttons. `addon` is the list model's row (the
// delegate's `model`), so a new model role is available here as `addon.role`
// with no extra plumbing.
ColumnLayout {
    id: root

    required property var addon
    readonly property var win: QQC2.ApplicationWindow.window

    spacing: Kirigami.Units.largeSpacing

    // ---- Facts -------------------------------------------------------------
    GridLayout {
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        QQC2.Label { text: qsTr("Version"); opacity: 0.7 }
        // The version, then the zip it came from (dimmer, shortened first when space is tight).
        RowLayout {
            spacing: Kirigami.Units.largeSpacing
            QQC2.Label {
                text: root.addon.versionText
                opacity: root.addon.linked ? 1 : 0.7
            }
            QQC2.Label {
                visible: root.addon.zipNameText.length > 0
                text: root.addon.zipNameText
                opacity: 0.6
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
        }

        QQC2.Label { text: qsTr("Release channel"); opacity: 0.7 }
        QQC2.Label { text: root.addon.channelText }

        QQC2.Label { text: qsTr("Flavor"); opacity: 0.7 }
        RowLayout {
            QQC2.Label { visible: root.addon.flavorTypeId != 0; text: root.addon.flavorName }
            // Only while the flavor is unknown: linking or updating an addon fills it in.
            QQC2.ComboBox {
                visible: root.addon.flavorTypeId == 0
                enabled: wam.flavors.length > 0
                model: wam.flavors
                textRole: "name"
                valueRole: "id"
                currentIndex: -1
                displayText: qsTr("Set flavor")
                onActivated: wam.setFlavor(root.addon.modId, currentValue)
            }
        }

        QQC2.Label { text: qsTr("Released"); opacity: 0.7; visible: root.addon.releasedText.length > 0 }
        QQC2.Label { text: root.addon.releasedText; visible: root.addon.releasedText.length > 0 }

        QQC2.Label { text: qsTr("Installed"); opacity: 0.7 }
        QQC2.Label { text: root.addon.installedText }

        QQC2.Label { text: qsTr("Source"); opacity: 0.7 }
        QQC2.Label { text: root.addon.sourceText }
    }

    // ---- Folders -----------------------------------------------------------
    QQC2.Label { text: qsTr("Folders (%1)").arg(root.addon.folders.length); opacity: 0.7 }
    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        Repeater {
            model: root.addon.folders
            delegate: FolderChip { required property string modelData; text: modelData }
        }
    }

    // ---- Changelog of the installed version ---------------------------------
    ColumnLayout {
        visible: root.addon.linked
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        QQC2.Label { text: qsTr("What's new in this version"); opacity: 0.7 }
        ChangelogBox {
            Layout.fillWidth: true
            loadState: root.addon.changelogState
            text: root.addon.changelogText
        }
    }

    // ---- Actions -----------------------------------------------------------
    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        // Only once there is a CurseForge file to point at: adopted addons get these after linking or updating.
        QQC2.Button {
            visible: root.addon.linked && root.addon.modSlug.length > 0
            icon.name: "internet-services"
            text: qsTr("View on CurseForge")
            onClicked: Qt.openUrlExternally(wam.modPageUrl(root.addon.modSlug))
        }
        QQC2.Button {
            visible: !root.addon.linked
            enabled: wam.hasApiKey
            icon.name: "insert-link"
            text: qsTr("Link to CurseForge…")
            onClicked: root.win.openLink(root.addon.modId, root.addon.displayName, root.addon.iconUrl, root.addon.flavorTypeId)
        }
        QQC2.Button {
            visible: root.addon.linked
            enabled: wam.hasApiKey
            icon.name: "download"
            text: qsTr("Install another version…")
            onClicked: root.win.openInstall(root.addon.modId, root.addon.displayName, root.addon.iconUrl, root.addon.flavorTypeId)
        }
    }
}
