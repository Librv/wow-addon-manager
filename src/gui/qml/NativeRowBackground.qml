import QtQuick
import org.kde.desktop.private as StylePrivate

// The desktop style's own list row highlight. Kept in a file of its own so the
// private import is only resolved when ListRowBackground loads this by name:
// if the module is missing or changed, only this file fails.
StylePrivate.DefaultListItemBackground {
    // Set by ListRowBackground: the ItemDelegate being painted.
    required property var owner
    control: owner
}
