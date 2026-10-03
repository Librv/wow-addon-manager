import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A small monospace tag, used for the AddOns folders an addon owns.
Rectangle {
    id: chip
    property string text

    implicitWidth: label.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: label.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: AppColors.border

    QQC2.Label {
        id: label
        anchors.centerIn: parent
        text: chip.text
        font.family: "monospace"
    }
}
