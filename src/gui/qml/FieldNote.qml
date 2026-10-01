import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The small grey explanation that sits under a setting.
QQC2.Label {
    Layout.maximumWidth: Kirigami.Units.gridUnit * 30
    wrapMode: Text.WordWrap
    opacity: 0.7
    font: Kirigami.Theme.smallFont
}
