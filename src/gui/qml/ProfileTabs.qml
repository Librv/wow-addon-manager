import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The profile tabs across the top of the page area, one per profile, with a
// dot on the active one. Selecting a tab makes it the current profile: the
// pages then show its addons. A right click (or the menu button) renames or
// deletes a profile; the trailing button creates one.
Rectangle {
    id: bar

    // ---- Tunables ---------------------------------------------------------
    property real tabHeight: Kirigami.Units.gridUnit * 2
    property real tabPadding: Kirigami.Units.largeSpacing * 1.5
    property real indicatorHeight: 2                          // the line under the selected tab
    property color indicatorColor: Kirigami.Theme.highlightColor
    property real hoverOpacity: 0.06                          // tint behind a hovered tab
    property real idleTextOpacity: 0.7                        // tabs that are not selected
    property real dotSize: Kirigami.Units.smallSpacing * 1.5  // the active marker
    property color dotColor: Kirigami.Theme.positiveTextColor
    property real dotGap: Kirigami.Units.largeSpacing         // between the name and the dot
    property real maxTabWidth: Kirigami.Units.gridUnit * 12   // a longer name is cut off with "…"
    property string activeTip: qsTr("Active profile: its addons are in the game folder")
    property string newIcon: "list-add"
    property string menuIcon: "overflow-menu"
    // ------------------------------------------------------------------------

    implicitHeight: tabHeight + 1
    color: AppColors.background

    // The profile the menu and the dialogs act on.
    property var target: ({ id: -1, name: "", active: false })

    function indexOfCurrent() {
        const p = wam.profiles
        for (let i = 0; i < p.length; ++i)
            if (p[i].id === wam.currentProfileId) return i
        return -1
    }
    function currentProfile() {
        const i = indexOfCurrent()
        return i >= 0 ? wam.profiles[i] : ({ id: -1, name: "", active: false })
    }
    function openMenu(profile, item) {
        target = profile
        dialogOf(menuLoader).popup(item)
    }
    // Menu and dialogs are built on first use; each is only needed on demand.
    function dialogOf(loader) { loader.active = true; return loader.item }

    Rectangle { // the edge line against the page
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: AppColors.border
    }

    RowLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: bar.tabHeight
        spacing: 0

        // A plain row rather than a TabBar: TabBar gives every tab the same
        // width, so a long name ran under the next tab. Each tab here is as
        // wide as its name, and the row scrolls sideways once they no longer fit.
        Flickable {
            id: tabs
            // Prefers the tabs' full width: with only fillWidth it split the free
            // space with the spacer below and clipped the last tab. It still
            // shrinks (and scrolls) when the window is too narrow.
            Layout.fillWidth: true
            Layout.preferredWidth: tabRow.width
            Layout.maximumWidth: tabRow.width
            Layout.fillHeight: true
            contentWidth: tabRow.width
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: tabRow
                height: tabs.height

                Repeater {
                    model: wam.profiles
                    delegate: QQC2.TabButton {
                        id: tab
                        required property var modelData
                        height: bar.tabHeight
                        leftPadding: bar.tabPadding
                        rightPadding: bar.tabPadding
                        // Follows the controller; a click only asks for the switch.
                        checkable: false
                        checked: modelData.id === wam.currentProfileId
                        // Breeze's TabButton reports a custom contentItem as 0 wide and
                        // falls back to a fixed minimum, so size the tab from its name.
                        // Measured in bold, so the selected tab does not grow.
                        // Capped at maxTabWidth, where the name is elided and the dot stays.
                        implicitWidth: Math.min(bar.maxTabWidth, leftPadding + rightPadding + Math.ceil(boldName.advanceWidth)
                                                + (modelData.active ? bar.dotGap + bar.dotSize : 0))

                        TextMetrics {
                            id: boldName
                            font.family: nameLabel.font.family
                            font.pointSize: nameLabel.font.pointSize
                            font.bold: true
                            text: tab.modelData.name
                        }

                        onClicked: if (modelData.id !== wam.currentProfileId) wam.selectProfile(modelData.id)

                        contentItem: RowLayout {
                            spacing: bar.dotGap
                            QQC2.Label {
                                id: nameLabel
                                Layout.fillWidth: true
                                Layout.preferredWidth: Math.ceil(boldName.advanceWidth)
                                text: tab.modelData.name
                                elide: Text.ElideRight
                                opacity: tab.checked ? 1 : bar.idleTextOpacity
                                font.bold: tab.checked
                            }
                            Rectangle {
                                visible: tab.modelData.active
                                Layout.preferredWidth: bar.dotSize
                                Layout.preferredHeight: bar.dotSize
                                radius: width / 2
                                color: bar.dotColor
                            }
                        }
                        background: Rectangle {
                            color: tab.hovered && !tab.checked
                                   ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, bar.hoverOpacity)
                                   : "transparent"
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: bar.indicatorHeight
                                visible: tab.checked
                                color: bar.indicatorColor
                            }
                        }

                        // The full name when it was cut off, and what the marker means.
                        readonly property string tip: [nameLabel.truncated ? modelData.name : "",
                                                       modelData.active ? bar.activeTip : ""].filter(t => t).join("\n")
                        QQC2.ToolTip.visible: hovered && tip !== ""
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        QQC2.ToolTip.text: tip

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: bar.openMenu(tab.modelData, tab)
                        }
                    }
                }
            }
        }

        QQC2.ToolButton {
            icon.name: bar.newIcon
            display: QQC2.AbstractButton.IconOnly
            text: qsTr("New profile…")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: bar.dialogOf(nameLoader).openNew()
        }

        Item { Layout.fillWidth: true }

        QQC2.ToolButton {
            id: menuButton
            icon.name: bar.menuIcon
            display: QQC2.AbstractButton.IconOnly
            text: qsTr("Profile options")
            enabled: bar.indexOfCurrent() >= 0
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.text: text
            onClicked: bar.openMenu(bar.currentProfile(), menuButton)
        }
    }

    Loader {
        id: menuLoader
        active: false
        sourceComponent: QQC2.Menu {
            QQC2.MenuItem {
                text: qsTr("Rename…")
                icon.name: "edit-rename"
                onTriggered: bar.dialogOf(nameLoader).openRename(bar.target.id, bar.target.name)
            }
            QQC2.MenuItem {
                text: qsTr("Delete…")
                icon.name: "edit-delete"
                // The active profile's addons are in the game folder, and there is always one profile.
                enabled: !bar.target.active && wam.profiles.length > 1
                onTriggered: {
                    const d = bar.dialogOf(deleteLoader)
                    d.target = bar.target
                    d.open()
                }
            }
        }
    }

    Loader {
        id: nameLoader
        active: false
        sourceComponent: ProfileNameDialog {}
    }

    Loader {
        id: deleteLoader
        active: false
        sourceComponent: Kirigami.PromptDialog {
            property var target: ({ id: -1, name: "" })
            title: qsTr("Delete %1?").arg(target.name)
            subtitle: qsTr("This deletes the profile and its addon list. The game's AddOns folder is not touched.")
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
            onAccepted: wam.deleteProfile(target.id)
        }
    }
}
