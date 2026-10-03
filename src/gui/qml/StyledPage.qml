import QtQuick
import org.kde.kirigami as Kirigami

// Base for every page, so they all share the app's background colour.
Kirigami.ScrollablePage {
    background: Rectangle { color: AppColors.background }
}
