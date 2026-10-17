import QtQuick
import org.kde.kirigami as Kirigami

// Background of a list row (set as an ItemDelegate's `background`). The desktop
// style paints a full-row image per row even when idle, about 1 MB each, so the
// highlight is only created while the row is hovered, pressed, focused or
// highlighted. If the native one cannot load, a plain tint shows instead: a
// changed private module costs the look of the highlight, never the row.
Loader {
    id: root

    // The ItemDelegate this is the background of.
    required property var control

    // Tunables: the fallback tint is Kirigami.Theme's highlight colour (AppColors
    // has no highlight colour) at this opacity.
    readonly property real fallbackOpacity: 0.15

    // The focus test matches the style's own background (`reallyFocus`), which also
    // counts focus given back with OtherFocusReason, e.g. after a popup closes.
    active: control.hovered || control.down || control.highlighted || control.visualFocus
            || (control.activeFocus && control.focusReason === Qt.OtherFocusReason)
    // Set at creation, not in onLoaded: `owner` is required, so a later assignment
    // comes too late and the load fails.
    Component.onCompleted: setSource("NativeRowBackground.qml", { owner: root.control })

    Rectangle {
        anchors.fill: parent
        visible: root.status === Loader.Error
        color: Kirigami.Theme.highlightColor
        opacity: root.fallbackOpacity
    }
}
