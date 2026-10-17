pragma Singleton
import QtQuick
import org.kde.kirigami as Kirigami

// The app's colour palette: one place to change how the UI looks. Every colour
// comes from the system colour scheme. They used to be fixed Breeze Dark values,
// but text follows the scheme, so a light scheme put dark text on dark surfaces.
// On Breeze Dark these give the same colours as before.
//
// (Not called "Palette": that clashes with a built-in Qt Quick type.)
QtObject {
    // Page background, behind every page and the sidebar: the scheme's window colour.
    readonly property color background: windowColors.Kirigami.Theme.backgroundColor
    // Inset surfaces: text fields, the changelog panel. The scheme's view colour,
    // darker than the page on dark schemes.
    readonly property color field: viewColors.Kirigami.Theme.backgroundColor
    // Outlines: separators, the changelog panel, the folder chips. Mixed the way
    // Kirigami.Separator mixes its colour, so they match KDE's own lines.
    readonly property color border: Kirigami.ColorUtils.linearInterpolation(
        background, windowColors.Kirigami.Theme.textColor, windowColors.Kirigami.Theme.frameContrast)

    // Holders for the two colour sets read above.
    readonly property QtObject windowColors: QtObject {
        Kirigami.Theme.colorSet: Kirigami.Theme.Window
        Kirigami.Theme.inherit: false
    }
    readonly property QtObject viewColors: QtObject {
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
    }
}
