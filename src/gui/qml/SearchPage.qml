import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

StyledPage {
    id: page
    title: qsTr("Search CurseForge")
    readonly property var win: QQC2.ApplicationWindow.window

    // Fill the search box and run the search (used when arriving from the scan window).
    function runQuery(text) {
        searchField.text = text
        if (wam.hasApiKey && text.length > 0) wam.search(text)
    }

    header: Kirigami.SearchField {
        id: searchField
        enabled: wam.hasApiKey
        onAccepted: wam.search(text)
    }

    ListView {
        id: list
        model: wam.searchResults

        delegate: QQC2.ItemDelegate {
            id: row
            required property var modId
            required property string name
            required property string author
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
                AuthorLine { author: row.author }
                QQC2.Label {
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    text: row.summary
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    opacity: 0.8
                    Layout.fillWidth: true
                }
              }
              QQC2.Button {
                  text: qsTr("Install")
                  icon.name: "download"
                  Layout.alignment: Qt.AlignVCenter
                  onClicked: page.win.openInstall(row.modId, row.name, row.logoUrl)
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
