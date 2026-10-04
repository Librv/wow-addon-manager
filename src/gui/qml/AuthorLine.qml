import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// "By Author", the line under an addon's title. Takes no space when the
// author is not known yet (adopted addons get it once CurseForge has been asked).
QQC2.Label {
    property string author

    visible: author.length > 0
    text: qsTr("By %1").arg(author)
    opacity: 0.7
    elide: Text.ElideRight
    Layout.fillWidth: true
}
