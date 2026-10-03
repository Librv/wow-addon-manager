import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

StyledPage {
    title: qsTr("Settings")

    Kirigami.FormLayout {
        RowLayout {
            Kirigami.FormData.label: qsTr("CurseForge API key:")
            QQC2.TextField {
                id: keyField
                echoMode: TextInput.Password
                placeholderText: wam.hasApiKey ? qsTr("(saved)") : qsTr("Paste your key")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            }
            QQC2.Button {
                text: qsTr("Save")
                enabled: keyField.text.length > 0
                onClicked: { wam.setApiKey(keyField.text); keyField.clear() }
            }
        }
        FieldNote {
            text: qsTr("Needed to search, install and update. Scanning your folder, adopting and removing addons work without it.")
        }

        RowLayout {
            Kirigami.FormData.label: qsTr("WoW folder:")
            QQC2.TextField { id: pathField; text: wam.wowPath; Layout.preferredWidth: Kirigami.Units.gridUnit * 20 }
            QQC2.Button { text: qsTr("Browse…"); onClicked: folderDialog.open() }
            QQC2.Button {
                text: qsTr("Save")
                enabled: pathField.text.length > 0 && pathField.text !== wam.wowPath
                onClicked: wam.setWowPath(pathField.text)
            }
        }
        FieldNote {
            text: qsTr("The game's flavor folder, for example …/World of Warcraft/_retail_. Addons are installed to Interface/AddOns inside it.")
        }

        QQC2.ComboBox {
            id: flavorBox
            Kirigami.FormData.label: qsTr("Flavor:")
            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            // Greyed out until there is a folder to describe (and a flavor list to choose from).
            enabled: wam.hasWowPath && wam.flavors.length > 0
            model: wam.flavors
            textRole: "name"
            valueRole: "id"
            displayText: currentIndex >= 0 ? currentText
                       : !wam.hasWowPath ? qsTr("Set the WoW folder first")
                       : qsTr("Not detected: pick one")

            function indexOfFlavor() {
                const f = wam.flavors
                for (let i = 0; i < f.length; ++i)
                    if (f[i].id === wam.wowFlavorId) return i
                return -1
            }
            // The user's own pick breaks a plain currentIndex binding, so follow the config explicitly.
            currentIndex: indexOfFlavor()
            onCountChanged: currentIndex = indexOfFlavor()
            onActivated: wam.setWowFlavor(currentValue)
            Connections {
                target: wam
                function onConfigChanged() { flavorBox.currentIndex = flavorBox.indexOfFlavor() }
            }
        }
        FieldNote {
            text: qsTr("Worked out from the folder name when you save the path, using the flavors below. If it cannot be worked out, pick it here. It is the default when installing; you can still choose another flavor for a single addon in the install window.")
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Flavors")
        }
        FieldNote {
            text: qsTr("The flavors CurseForge defines, refreshed each time the app starts. The key on the left is CurseForge's; change the name to show something else. Your names are kept when the list refreshes, and the reset button restores CurseForge's name.")
        }

        Repeater {
            model: wam.flavorEntries
            delegate: RowLayout {
                id: row
                required property string slug
                required property string name
                required property string apiName
                required property bool edited
                Kirigami.FormData.label: row.slug + ":"

                QQC2.TextField {
                    id: nameField
                    text: row.name
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 16
                    onEditingFinished: {
                        wam.renameFlavor(row.slug, text)
                        // An emptied field means "reset": follow the model again.
                        if (text.trim().length === 0) text = Qt.binding(function() { return row.name })
                    }
                }
                QQC2.ToolButton {
                    icon.name: "edit-undo"
                    visible: row.edited
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: qsTr("Reset to CurseForge's name (%1)").arg(row.apiName)
                    onClicked: { wam.renameFlavor(row.slug, ""); nameField.text = Qt.binding(function() { return row.name }) }
                }
            }
        }
        FieldNote {
            visible: wam.flavorEntries.count === 0
            text: qsTr("No flavors yet. They are fetched from CurseForge when the app starts with an API key.")
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: pathField.text = wam.localPath(selectedFolder)
    }
}
