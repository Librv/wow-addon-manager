import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
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

        RowLayout {
            Kirigami.FormData.label: qsTr("Flavor:")
            QQC2.ComboBox {
                id: flavorBox
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                // Greyed out until there is a folder to describe (and a flavor list to choose from).
                enabled: wam.hasWowPath && wam.flavors.length > 0
                model: wam.flavors
                textRole: "name"
                valueRole: "id"
                displayText: currentIndex >= 0 ? currentText
                           : !wam.hasWowPath ? qsTr("Set the WoW folder first")
                           : !wam.hasApiKey ? qsTr("Add an API key to detect it")
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
        }

        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 28
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("Point this at the game flavor folder, for example …/World of Warcraft/_retail_. Addons are installed to Interface/AddOns inside it. The flavor is worked out from the folder name when you save the path (it needs your API key); pick it here if it could not be worked out. It is the default when installing, and you can still choose another flavor for a single addon in the install window.")
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: pathField.text = wam.localPath(selectedFolder)
    }
}
