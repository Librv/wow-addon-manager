import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One installed addon in the list: the clickable header (the same size as a
// Search result) and, when `open`, its details underneath. Used as the
// ListView delegate in InstalledPage, so `model` is the row.
//
// The row does not decide what happens: it only raises signals, and the page
// owns the open/closed state and the remove dialog.
Item {
    id: root

    required property var model
    property bool open: false

    signal toggleRequested()
    signal removeRequested()
    signal stopTrackingRequested()
    signal checkRequested()

    width: ListView.view ? ListView.view.width : implicitWidth
    height: col.implicitHeight

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 0

        QQC2.ItemDelegate {
            Layout.fillWidth: true
            // The same space above the title as below the description.
            topPadding: Kirigami.Units.largeSpacing
            bottomPadding: Kirigami.Units.largeSpacing
            onClicked: root.toggleRequested()

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                AddonIcon { source: root.model.iconUrl; size: Kirigami.Units.iconSizes.large; Layout.alignment: Qt.AlignTop }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Label { text: root.model.displayName; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    AuthorLine { author: root.model.author }
                    AddonMeta {
                        Layout.topMargin: Kirigami.Units.largeSpacing
                        downloads: root.model.downloadsText
                        date: root.model.releasedText
                        version: root.model.shortVersionText
                        // An addon not linked to a file yet has no version or date to show.
                        flavor: root.model.linked ? root.model.flavorName
                              : [qsTr("Adopted"), root.model.flavorName].filter(s => s.length > 0).join(" \u00b7 ")
                    }
                }

                QQC2.ToolButton {
                    icon.name: "view-refresh"
                    enabled: wam.hasApiKey
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: qsTr("Check %1 for updates").arg(root.model.displayName)
                    onClicked: root.checkRequested()
                }
                QQC2.ToolButton {
                    icon.name: root.open ? "arrow-up" : "arrow-down"
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: root.open ? qsTr("Hide details") : qsTr("Show details")
                    onClicked: root.toggleRequested()
                }
                QQC2.ToolButton {
                    icon.name: "overflow-menu"
                    onClicked: rowMenu.open()
                    QQC2.Menu {
                        id: rowMenu
                        QQC2.MenuItem { text: qsTr("Remove…"); onTriggered: root.removeRequested() }
                        QQC2.MenuItem { text: qsTr("Stop tracking (keep files)"); onTriggered: root.stopTrackingRequested() }
                    }
                }
            }
        }

        AddonDetails {
            visible: root.open
            addon: root.model
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.iconSizes.large + Kirigami.Units.largeSpacing * 2
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
        }

        Kirigami.Separator { Layout.fillWidth: true }
    }
}
