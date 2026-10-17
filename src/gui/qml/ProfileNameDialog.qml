import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Asks for a profile name: either for a new profile (openNew) or to rename an
// existing one (openRename). A new profile copies the current one's folder and
// flavor, starts empty and inactive, and becomes the selected tab.
Kirigami.Dialog {
    id: dialog

    // ---- Tunables ---------------------------------------------------------
    property string newTitle: qsTr("New profile")
    property string renameTitle: qsTr("Rename profile")
    property string createText: qsTr("Create")
    property string renameText: qsTr("Rename")
    property string placeholder: qsTr("Profile name")
    property string newNote: qsTr("Starts empty, with this profile's WoW folder and flavor. You can change them in Settings.")
    // ------------------------------------------------------------------------

    property int profileId: -1      // -1: create a new profile
    readonly property string name: nameField.text.trim()

    function openNew() {
        profileId = -1
        nameField.text = ""
        open()
    }
    function openRename(id, currentName) {
        profileId = id
        nameField.text = currentName
        open()
    }
    function submit() {
        if (name.length === 0) return
        if (profileId < 0) wam.createProfile(name)
        else wam.renameProfile(profileId, name)
        close()
    }

    title: profileId < 0 ? newTitle : renameTitle
    preferredWidth: Kirigami.Units.gridUnit * 22
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: dialog.profileId < 0 ? dialog.createText : dialog.renameText
            icon.name: dialog.profileId < 0 ? "list-add" : "edit-rename"
            enabled: dialog.name.length > 0
            onTriggered: dialog.submit()
        }
    ]

    onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        QQC2.TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: dialog.placeholder
            onAccepted: dialog.submit()
        }
        FieldNote {
            visible: dialog.profileId < 0
            Layout.fillWidth: true
            text: dialog.newNote
        }
    }
}
