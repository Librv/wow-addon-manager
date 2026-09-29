import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    title: qsTr("Search CurseForge")
    readonly property var win: QQC2.ApplicationWindow.window
    property string initialQuery: ""   // set when arriving from the scan page

    Component.onCompleted: if (initialQuery.length > 0 && wam.hasApiKey) wam.search(initialQuery)

    header: Kirigami.SearchField {
        enabled: wam.hasApiKey
        text: page.initialQuery
        onAccepted: wam.search(text)
    }

    ListView {
        id: list
        model: wam.searchResults

        delegate: QQC2.ItemDelegate {
            id: row
            required property var modId
            required property string name
            required property string summary
            required property string logoUrl
            width: ListView.view.width

            contentItem: RowLayout {
              spacing: Kirigami.Units.largeSpacing
              AddonIcon { source: row.logoUrl; size: Kirigami.Units.iconSizes.large; Layout.alignment: Qt.AlignTop }
              ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                QQC2.Label { text: row.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                QQC2.Label {
                    text: row.summary
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    opacity: 0.8
                    Layout.fillWidth: true
                }
                RowLayout {
                    QQC2.ComboBox {
                        id: flavorBox
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                        textRole: "name"
                        valueRole: "id"
                        // reading wam.flavors makes this re-evaluate once the flavor list loads
                        model: { const dep = wam.flavors; return wam.flavorsForMod(row.modId) }
                        currentIndex: count === 1 ? 0 : -1 // must pick, unless there is only one choice
                        displayText: currentIndex < 0 ? qsTr("Pick a flavor") : currentText
                    }
                    QQC2.ComboBox { id: channelBox; model: ["release", "beta", "alpha"] }
                    Item { Layout.fillWidth: true }
                    QQC2.Button {
                        text: qsTr("Install")
                        icon.name: "download"
                        enabled: wam.hasWowPath && flavorBox.currentIndex >= 0
                        onClicked: {
                            page.win.showPassiveNotification(qsTr("Installing %1…").arg(row.name))
                            wam.install(row.modId, channelBox.currentText, flavorBox.currentValue)
                        }
                    }
                }
              }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            text: wam.hasApiKey ? qsTr("Search for an addon above") : qsTr("A CurseForge API key is required")
            helpfulAction: Kirigami.Action {
                visible: !wam.hasApiKey
                text: qsTr("Open settings")
                onTriggered: page.win.showSettings()
            }
        }
    }
}
