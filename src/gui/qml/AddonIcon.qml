import QtQuick
import org.kde.kirigami as Kirigami

// An addon's CurseForge logo, with a generic package icon while it loads or
// when the addon has none.
Item {
    id: root
    property string source
    property int size: Kirigami.Units.iconSizes.large

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        visible: status === Image.Ready
        source: root.source
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: root.size * 2   // 2x so it stays sharp on HiDPI
        sourceSize.height: root.size * 2
    }
    Kirigami.Icon {
        anchors.fill: parent
        visible: img.status !== Image.Ready
        source: "package-x-generic"
    }
}
