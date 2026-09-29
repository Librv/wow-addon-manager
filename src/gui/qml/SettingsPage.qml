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

        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 28
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("Point this at the game flavor folder, for example …/World of Warcraft/_retail_. Addons are installed to Interface/AddOns inside it.")
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: pathField.text = wam.localPath(selectedFolder)
    }
}
