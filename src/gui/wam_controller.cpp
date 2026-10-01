#include "gui/wam_controller.hpp"
#include "gui/wam_meta_types.hpp"
#include <QMetaObject>

namespace wam::gui {

namespace {
QVariantMap flavorMap(const FlavorInfo& f) {
    return {{"id", f.id}, {"name", f.name}};
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
                // No-op when every addon already has its icon, slug and file details.
                if (hasKey) QMetaObject::invokeMethod(worker_, "backfillDetails", Qt::QueuedConnection);
            });

    connect(worker_, &WamWorker::flavorsChanged, this, [this](const QList<wam::gui::FlavorInfo>& flavors) {
        flavorEntries_.setEntries(flavors);
        QHash<qint64, QString> names;
        for (const auto& f : flavors) names.insert(f.id, f.name);
        installedAddons_.setFlavorNames(names); // rows show the current display name
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
                if (singleChecks_.contains(modId)) emit updateFound(modId);
            });
    connect(worker_, &WamWorker::upToDate, this, [this](qint64 modId, const QString& name) {
        pendingUpdates_.remove(modId); // a stale row for something that is current now
        if (singleChecks_.contains(modId)) emit addonUpToDate(modId, name);
    });
    connect(worker_, &WamWorker::singleCheckFinished, this, [this](qint64 modId) { singleChecks_.remove(modId); });
    connect(worker_, &WamWorker::linked, this, &WamController::linked);
    connect(worker_, &WamWorker::changelogLoaded, this, [this](qint64 fileId, const QString& text) {
        installedAddons_.setChangelog(fileId, "ready", text);
    });
    connect(worker_, &WamWorker::changelogFailed, this, [this](qint64 fileId, const QString& message) {
        installedAddons_.setChangelog(fileId, "failed", message);
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
    for (const auto& f : flavorEntries_.entries()) out.push_back(flavorMap(f));
    return out;
}

QVariantList WamController::flavorsForInstall(qint64 modId) const {
    const auto ids = searchResults_.flavorIdsFor(modId);
    if (ids.isEmpty()) return flavors(); // not in the search results, or the mod reports none: offer everything
    QVariantList out;
    for (const auto& f : flavorEntries_.entries())
        if (ids.contains(f.id) || f.id == wowFlavorId_) out.push_back(flavorMap(f));
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

void WamController::renameFlavor(const QString& slug, const QString& name) {
    QMetaObject::invokeMethod(worker_, "renameFlavor", Qt::QueuedConnection,
                              Q_ARG(QString, slug), Q_ARG(QString, name));
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

QString WamController::downloadUrl(qint64 modId, qint64 fileId) const {
    return QString::fromStdString(wam::CurseForgeClient::browserDownloadUrl(modId, fileId));
}

QString WamController::modPageUrl(const QString& slug) const {
    return QString::fromStdString(wam::CurseForgeClient::modPageUrl(slug.toStdString()));
}

void WamController::checkUpdate(qint64 modId) {
    singleChecks_.insert(modId);
    QMetaObject::invokeMethod(worker_, "checkOneUpdate", Qt::QueuedConnection, Q_ARG(qint64, modId));
}

void WamController::loadChangelog(qint64 modId) {
    const qint64 fileId = installedAddons_.fileIdFor(modId);
    if (fileId == 0) return; // adopted and not linked: there is no file to ask about
    const QString state = installedAddons_.changelogState(fileId);
    if (state == "loading" || state == "ready") return; // a failed one may be retried
    installedAddons_.setChangelog(fileId, "loading", {});
    QMetaObject::invokeMethod(worker_, "loadChangelog", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(qint64, fileId));
}

void WamController::linkFile(qint64 modId, qint64 fileId, qint64 flavorTypeId) {
    QMetaObject::invokeMethod(worker_, "linkFile", Qt::QueuedConnection,
                              Q_ARG(qint64, modId), Q_ARG(qint64, fileId), Q_ARG(qint64, flavorTypeId));
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
