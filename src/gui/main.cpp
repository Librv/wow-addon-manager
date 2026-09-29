#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "gui/wam_controller.hpp"

int main(int argc, char** argv) {
    // QApplication rather than QGuiApplication, as far as I know, so
    // qqc2-desktop-style can pick the native KDE look under Plasma.
    QApplication app(argc, argv);
    QApplication::setApplicationName("wow-addon-manager");
    QApplication::setDesktopFileName("wow-addon-manager");

    wam::gui::WamController controller; // declared before the engine so it outlives it
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("wam", &controller);
    engine.loadFromModule("Wam", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
