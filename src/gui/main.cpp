#include <QApplication>
#include <QDir>
#include <QFileSystemWatcher>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include "gui/wam_controller.hpp"

// UI development mode: WAM_QML_DIR=/path/to/src/gui/qml loads the QML straight
// from disk instead of the copy compiled into the binary, and reloads the
// window whenever a .qml file in that folder is saved. No rebuild needed.
// Unset, the app behaves exactly as before.
namespace {

void watchQmlDir(QQmlApplicationEngine& engine, const QString& dir) {
    auto* watcher = new QFileSystemWatcher(&engine);
    auto* debounce = new QTimer(&engine);
    debounce->setSingleShot(true);
    debounce->setInterval(150);

    auto rewatch = [watcher, dir] {
        // Editors often replace the file on save, which drops it from the watcher.
        QStringList paths{dir};
        for (const auto& f : QDir(dir).entryList({"*.qml"}, QDir::Files)) paths << QDir(dir).filePath(f);
        if (!watcher->files().isEmpty()) watcher->removePaths(watcher->files());
        if (!watcher->directories().isEmpty()) watcher->removePaths(watcher->directories());
        watcher->addPaths(paths);
    };
    auto load = [&engine, dir] {
        engine.clearComponentCache();
        engine.load(QUrl::fromLocalFile(QDir(dir).filePath("Main.qml")));
    };

    QObject::connect(debounce, &QTimer::timeout, &engine, [&engine, load, rewatch] {
        // Closing the old window must not end the app before the new one is up.
        QGuiApplication::setQuitOnLastWindowClosed(false);
        const auto roots = engine.rootObjects();
        for (QObject* o : roots) delete o;
        load();
        rewatch();
        QTimer::singleShot(0, [] { QGuiApplication::setQuitOnLastWindowClosed(true); });
    });
    auto trigger = [debounce] { debounce->start(); };
    QObject::connect(watcher, &QFileSystemWatcher::fileChanged, &engine, trigger);
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, &engine, trigger);
    rewatch();
}

} // namespace

int main(int argc, char** argv) {
    // QApplication rather than QGuiApplication, as far as I know, so
    // qqc2-desktop-style can pick the native KDE look under Plasma.
    QApplication app(argc, argv);
    QApplication::setApplicationName("wow-addon-manager");
    QApplication::setDesktopFileName("wow-addon-manager");

    wam::gui::WamController controller; // declared before the engine so it outlives it
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("wam", &controller);

    const QString devDir = qEnvironmentVariable("WAM_QML_DIR");
    if (!devDir.isEmpty()) {
        qInfo().noquote() << "UI dev mode: loading and watching" << QDir(devDir).absolutePath();
        engine.load(QUrl::fromLocalFile(QDir(devDir).absoluteFilePath("Main.qml")));
        watchQmlDir(engine, QDir(devDir).absolutePath());
        // A broken file on first start is not fatal: fix it and save, the window appears.
    } else {
        engine.loadFromModule("Wam", "Main");
        if (engine.rootObjects().isEmpty()) return 1;
    }
    return app.exec();
}
