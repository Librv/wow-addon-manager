#include "gui/wam_controller.hpp"
#include "gui/wam_meta_types.hpp"
#include <QMetaObject>

namespace wam::gui {

namespace {
QVariantMap flavorMap(const wam::GameVersionType& t) {
    return {{"id", static_cast<qint64>(t.id)}, {"name", QString::fromStdString(t.name)}};
}
} // namespace

WamController::WamController(QObject* parent) : QObject(parent) {
    registerMetaTypes();

    worker_ = new WamWorker(); // parentless: moveToThread requires it
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &WamWorker::configChanged, this,
            [this](bool hasKey, bool hasPath, const QString& path, qint64 flavorId, const QString& flavorName) {
                hasApiKey_ = hasKey;
                hasWowPath_ = hasPath;
                wowPath_ = path;
                wowFlavorId_ = flavorId;
                wowFlavorName_ = flavorName;
                emit configChanged();
                // The client caches this, so repeat requests cost no network.
                if (hasKey) {
                    QMetaObject::invokeMethod(worker_, "listGameVersionTypes", Qt::QueuedConnection);
                    // No-op when every addon already has an icon.
                    QMetaObject::invokeMethod(worker_, "backfillIcons", Qt::QueuedConnection);
                }
            });

    connect(worker_, &WamWorker::gameVersionTypesLoaded, this,
            [this](const QList<wam::GameVersionType>& types) {
                flavorTypes_ = types;
                emit flavorsChanged();
            });

    connect(worker_, &WamWorker::errorOccurred, this, &WamController::errorOccurred);

    connect(worker_, &WamWorker::searchFinished, this,
            [this](const QString&, const QList<wam::CurseForgeMod>& r) { searchResults_.setResults(r); });

    connect(worker_, &WamWorker::installFilesLoaded, this,
            [this](qint64 modId, qint64 flavorId, int index, const QList<wam::CurseForgeFile>& files, int total) {
                if (!installFiles_.matches(modId, flavorId)) return; // the dialog moved on to another addon/flavor
                QList<InstallFile> rows;
                for (const auto& f : files) rows.push_back(InstallFile::fromCurseForge(f));
                installFiles_.append(rows, index, total);
                fetchInstallPageIfNeeded();
            });
    connect(worker_, &WamWorker::installFilesFailed, this,
            [this](qint64 modId, qint64 flavorId, const QString& message) {
                if (installFiles_.matches(modId, flavorId)) installFiles_.fail(message);
            });

    connect(worker_, &WamWorker::installFinished, this, [this](const wam::InstalledAddon& a) {
        pendingUpdates_.remove(a.modId); // a manual install also resolves a queued update
        installFlavors_.remove(a.modId);
        emit installFinished(a.modId, QString::fromStdString(a.displayName));
    });
    connect(worker_, &WamWorker::downloadBlocked, this, &WamController::downloadBlocked);

    connect(worker_, &WamWorker::updateAvailable, this,
            [this](qint64 modId, const QString& name, const QString& slug, const QString& iconUrl,
                   const QString& flavor, const wam::CurseForgeFile& cur, const wam::CurseForgeFile& latest) {
                PendingUpdate u;
                u.modId = modId;
                u.modName = name;
                u.modSlug = slug;
                u.iconUrl = iconUrl;
                u.flavorName = flavor;
                u.currentFileName = cur.id == 0 ? QStringLiteral("unknown (adopted, never pinned)")
                                                : QString::fromStdString(cur.fileName);
                u.latestFileName = QString::fromStdString(latest.fileName);
                u.latestFileId = latest.id;
                u.blocked = latest.isBlocked();
                pendingUpdates_.add(u);
            });

    connect(worker_, &WamWorker::updateApplied, this, [this](const wam::InstalledAddon& a) {
        pendingUpdates_.remove(a.modId);
        emit updateApplied(a.modId, QString::fromStdString(a.displayName));
    });
    connect(worker_, &WamWorker::updateFailed, this, [this](qint64 modId, const QString& msg) {
        pendingUpdates_.setStatus(modId, PendingUpdate::Status::Failed, msg);
    });
    connect(worker_, &WamWorker::allUpdatesChecked, this, [this] {
        checking_ = false;
        emit checkingUpdatesChanged();
        emit updateCheckFinished(pendingUpdates_.count());
    });

    connect(worker_, &WamWorker::scanFinished, this, [this](const QList<wam::gui::ScanGroup>& groups) {
        scanResults_.setGroups(groups);
        scanning_ = false;
        emit scanningChanged();
    });
    connect(worker_, &WamWorker::adopted, this, &WamController::adopted);

    connect(worker_, &WamWorker::addonListLoaded, this,
            [this](const QList<wam::InstalledAddon>& a) { installedAddons_.setAddons(a); });

    thread_.start();
    QMetaObject::invokeMethod(worker_, "initialize", Qt::QueuedConnection);
}

WamController::~WamController() {
    thread_.quit();
    thread_.wait();
}

QVariantList WamController::flavors() const {
    QVariantList out;
    for (const auto& t : flavorTypes_) out.push_back(flavorMap(t));
    return out;
}

QVariantList WamController::flavorsForInstall(qint64 modId) const {
    const auto ids = searchResults_.flavorIdsFor(modId);
    QVariantList out;
    for (const auto& t : flavorTypes_) {
        const qint64 id = static_cast<qint64>(t.id);
        if (ids.contains(id) || id == wowFlavorId_) out.push_back(flavorMap(t));
    }
    return out.isEmpty() ? flavors() : out;
}

void WamController::setApiKey(const QString& key) {
    QMetaObject::invokeMethod(worker_, "setApiKey", Qt::QueuedConnection, Q_ARG(QString, key));
}
void WamController::setWowPath(const QString& path) {
    QMetaObject::invokeMethod(worker_, "setWowPath", Qt::QueuedConnection, Q_ARG(QString, path));
}
void WamController::search(const QString& query) {
    QMetaObject::invokeMethod(worker_, "search", Qt::QueuedConnection, Q_ARG(QString, query));
}
void WamController::setWowFlavor(qint64 flavorTypeId) {
    QMetaObject::invokeMethod(worker_, "setWowFlavor", Qt::QueuedConnection, Q_ARG(qint64, flavorTypeId));
}

void WamController::loadInstallFiles(qint64 modId, qint64 flavorTypeId) {
    installFiles_.begin(modId, flavorTypeId);
    autoInstallFetches_ = 0;
    fetchInstallPage();
}

void WamController::clearInstallFiles() {
    installFiles_.clear();
}

void WamController::setInstallChannel(const QString& channel) {
    installFiles_.setChannel(channel);
    autoInstallFetches_ = 0;
    fetchInstallPageIfNeeded(); // e.g. no beta among the versions loaded so far
}

void WamController::showMoreInstallFiles() {
    installFiles_.showMore();
    autoInstallFetches_ = 0;
    fetchInstallPageIfNeeded();
}

// Asks the worker for the page after the last one received.
void WamController::fetchInstallPage() {
    installFiles_.setLoading(true);
    QMetaObject::invokeMethod(worker_, "loadInstallFiles", Qt::QueuedConnection,
                              Q_ARG(qint64, installFiles_.modIdValue()), Q_ARG(qint64, installFiles_.flavorIdValue()),
                              Q_ARG(int, installFiles_.nextIndex()));
}

// Pages in more files only while the visible window is short and the server
// has more, and never more than a few pages per user action.
void WamController::fetchInstallPageIfNeeded() {
    if (!installFiles_.wantsFetch() || autoInstallFetches_ >= 3) return;
    ++autoInstallFetches_;
    fetchInstallPage();
}

void WamController::installFile(qint64 modId, qint64 fileId, qint64 flavorTypeId) {
    installFlavors_[modId] = flavorTypeId;
    QMetaObject::invokeMethod(worker_, "installFile", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(qint64, fileId), Q_ARG(qint64, flavorTypeId));
}
void WamController::installManual(qint64 modId, qint64 fileId, const QString& zipPath) {
    QMetaObject::invokeMethod(worker_, "installManual", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(qint64, fileId), Q_ARG(QString, zipPath),
                              Q_ARG(qint64, installFlavors_.value(modId, 0)));
}
void WamController::setFlavor(qint64 modId, qint64 flavorTypeId) {
    QMetaObject::invokeMethod(worker_, "setFlavor", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(qint64, flavorTypeId));
}

void WamController::checkAllUpdates() {
    if (checking_) return;
    pendingUpdates_.clear();
    checking_ = true;
    emit checkingUpdatesChanged();
    QMetaObject::invokeMethod(worker_, "checkAllUpdates", Qt::QueuedConnection);
}

void WamController::dispatchApply(qint64 modId, qint64 fileId) {
    pendingUpdates_.setStatus(modId, PendingUpdate::Status::Applying);
    QMetaObject::invokeMethod(worker_, "applyUpdate", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(qint64, fileId));
}

void WamController::applyUpdate(qint64 modId) {
    const auto* u = pendingUpdates_.find(modId);
    if (!u || u->blocked || u->status == PendingUpdate::Status::Applying) return;
    dispatchApply(u->modId, u->latestFileId);
}

void WamController::applyAllUpdates() {
    for (const auto& u : pendingUpdates_.items()) // a copy: rows change status while we loop
        if (!u.blocked && u.status == PendingUpdate::Status::Pending)
            dispatchApply(u.modId, u.latestFileId);
}

void WamController::skipUpdate(qint64 modId) {
    const auto* u = pendingUpdates_.find(modId);
    if (u && u->status != PendingUpdate::Status::Applying) pendingUpdates_.remove(modId);
}

void WamController::scan() {
    if (scanning_) return;
    scanning_ = true;
    emit scanningChanged();
    QMetaObject::invokeMethod(worker_, "scan", Qt::QueuedConnection);
}

void WamController::adopt(qint64 modId, const QStringList& folders, bool includeSiblings) {
    QMetaObject::invokeMethod(worker_, "adopt", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(QStringList, folders),
                              Q_ARG(bool, includeSiblings), Q_ARG(bool, true));
}

void WamController::adoptAllMatched() {
    const auto groups = scanResults_.groups(); // a copy: the model resets when the rescan lands
    QList<ScanGroup> matched;
    for (const auto& g : groups) if (g.modId != 0) matched.push_back(g);
    for (int i = 0; i < matched.size(); ++i) {
        // Rescan only after the last one, not once per row.
        QMetaObject::invokeMethod(worker_, "adopt", Qt::QueuedConnection,
                                  Q_ARG(qint64, matched[i].modId), Q_ARG(QStringList, matched[i].folders),
                                  Q_ARG(bool, true), Q_ARG(bool, i == matched.size() - 1));
    }
}

void WamController::removeAddon(qint64 modId) {
    QMetaObject::invokeMethod(worker_, "removeAddon", Qt::QueuedConnection, Q_ARG(qint64, modId));
}
void WamController::untrackAddon(qint64 modId) {
    QMetaObject::invokeMethod(worker_, "untrackAddon", Qt::QueuedConnection, Q_ARG(qint64, modId));
}

} // namespace wam::gui
