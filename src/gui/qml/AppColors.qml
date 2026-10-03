pragma Singleton
import QtQuick

// The app's colour palette: one place to change how the UI looks. Values were
// taken from the KDE Breeze Dark settings page. They are fixed hex values, so
// the surfaces look the same whatever the system colour scheme is; text and
// icon colours still follow the system theme.
//
// (Not called "Palette": that clashes with a built-in Qt Quick type.)
QtObject {
    // Page background, behind every page and the sidebar.
    readonly property color background: "#202326"
    // Inset surfaces: text fields, the changelog panel. Darker than the page.
    readonly property color field: "#141618"
    // Outlines: separators, the changelog panel, the folder chips.
    readonly property color border: "#474c51"
}
