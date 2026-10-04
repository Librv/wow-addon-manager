import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The facts line under an addon's description, the same on every tab:
// downloads, latest update date, latest version, then (where given) the
// flavor. The first three have an icon in front; empty ones are left out.
Flow {
    id: root

    property string downloads
    property string date
    property string version
    property string flavor   // no icon

    // ---- Tunables ---------------------------------------------------------
    property string downloadsIcon: "download"
    property string dateIcon: "clock"
    property string versionIcon: "preferences-plugin"   // a puzzle piece in Breeze
    property real itemOpacity: 0.7
    // ------------------------------------------------------------------------

    readonly property var entries: [
        { icon: downloadsIcon, text: downloads },
        { icon: dateIcon, text: date },
        { icon: versionIcon, text: version },
        { icon: "", text: flavor }
    ].filter(e => e.text.length > 0)

    visible: entries.length > 0
    spacing: Kirigami.Units.largeSpacing
    Layout.fillWidth: true

    Repeater {
        model: root.entries
        delegate: RowLayout {
            required property var modelData
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                visible: modelData.icon.length > 0
                source: modelData.icon
                opacity: root.itemOpacity
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: modelData.text
                opacity: root.itemOpacity
            }
        }
    }
}
