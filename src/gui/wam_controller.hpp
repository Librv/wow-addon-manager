#pragma once
#include <QObject>
#include <QThread>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include "gui/wam_worker.hpp"
#include "gui/installed_addons_model.hpp"
#include "gui/search_results_model.hpp"
#include "gui/pending_updates_model.hpp"
#include "gui/scan_results_model.hpp"
#include "gui/install_files_model.hpp"
#include "gui/flavors_model.hpp"
#include <QStringList>

namespace wam::gui {

// The only wam_core-facing object the GUI thread (and QML) talks to. Owns
// the worker thread and the list models; every slot here just forwards to
// WamWorker via a queued invokeMethod call and returns immediately. Results
// arrive later through signals or model updates. Direct member calls into
// WamWorker are never made from here; that would run its blocking code on
// the GUI thread.
class WamController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY configChanged)
    Q_PROPERTY(bool hasWowPath READ hasWowPath NOTIFY configChanged)
    Q_PROPERTY(QString wowPath READ wowPath NOTIFY configChanged)
    Q_PROPERTY(qint64 wowFlavorId READ wowFlavorId NOTIFY configChanged)     // 0 = not known
    Q_PROPERTY(QString wowFlavorName READ wowFlavorName NOTIFY configChanged)
    Q_PROPERTY(wam::gui::SearchResultsModel* searchResults READ searchResults CONSTANT)
    Q_PROPERTY(wam::gui::InstalledAddonsModel* installedAddons READ installedAddons CONSTANT)
    Q_PROPERTY(wam::gui::PendingUpdatesModel* pendingUpdates READ pendingUpdates CONSTANT)
    Q_PROPERTY(wam::gui::FlavorsModel* flavorEntries READ flavorEntries CONSTANT)  // the editable flavor list
    Q_PROPERTY(wam::gui::InstallFilesModel* installFiles READ installFiles CONSTANT)
    Q_PROPERTY(wam::gui::ScanResultsModel* scanResults READ scanResults CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList flavors READ flavors NOTIFY flavorsChanged)   // [{id, name}, ...]
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY checkingUpdatesChanged)

public:
    explicit WamController(QObject* parent = nullptr);
    ~WamController() override;

    bool hasApiKey() const { return hasApiKey_; }
    bool hasWowPath() const { return hasWowPath_; }
    QString wowPath() const { return wowPath_; }
    qint64 wowFlavorId() const { return wowFlavorId_; }
    QString wowFlavorName() const { return wowFlavorName_; }
    InstallFilesModel* installFiles() { return &installFiles_; }
    FlavorsModel* flavorEntries() { return &flavorEntries_; }
    bool checkingUpdates() const { return checking_; }
    SearchResultsModel* searchResults() { return &searchResults_; }
    InstalledAddonsModel* installedAddons() { return &installedAddons_; }
    PendingUpdatesModel* pendingUpdates() { return &pendingUpdates_; }
    ScanResultsModel* scanResults() { return &scanResults_; }
    bool scanning() const { return scanning_; }
    QVariantList flavors() const;

    // Flavors offered in the install dialog for a search result: the ones the
    // mod has files for, plus the WoW folder's own flavor so it can always be
    // the default. All flavors if the mod reports none. Cached data only.
    Q_INVOKABLE QVariantList flavorsForInstall(qint64 modId) const;
    Q_INVOKABLE QString localPath(const QUrl& url) const { return url.toLocalFile(); }
    // Where a browser gets a file from (for downloads the author has blocked
    // for other tools), and an addon's page on the website.
    Q_INVOKABLE QString downloadUrl(qint64 modId, qint64 fileId) const;
    Q_INVOKABLE QString modPageUrl(const QString& slug) const;

public slots:
    void setApiKey(const QString& key);
    void setWowPath(const QString& path);
    void search(const QString& query);
    void setWowFlavor(qint64 flavorTypeId); // 0 clears it
    void renameFlavor(const QString& slug, const QString& name); // empty name = reset to CurseForge's

    // Install dialog: the version list for one addon and flavor.
    void loadInstallFiles(qint64 modId, qint64 flavorTypeId);
    void clearInstallFiles();
    void setInstallChannel(const QString& channel);
    void showMoreInstallFiles();
    void installFile(qint64 modId, qint64 fileId, qint64 flavorTypeId);
    void installManual(qint64 modId, qint64 fileId, const QString& zipPath);
    void setFlavor(qint64 modId, qint64 flavorTypeId);

    void checkAllUpdates();
    void checkUpdate(qint64 modId);   // one addon; see addonUpToDate / updateFound
    void loadChangelog(qint64 modId); // fills the addon's changelog in the installed list
    void linkFile(qint64 modId, qint64 fileId, qint64 flavorTypeId);    void applyUpdate(qint64 modId);   // applies one; its row leaves the queue when done
    void applyAllUpdates();           // every pending, non-blocked row
    void skipUpdate(qint64 modId);    // drops the row without applying

    void scan();
    void adopt(qint64 modId, const QStringList& folders, bool includeSiblings);
    void adoptAllMatched(); // every row a .toc tag already identified

    void removeAddon(qint64 modId);
    void untrackAddon(qint64 modId);

signals:
    void configChanged();
    void flavorsChanged();
    void scanningChanged();
    void adopted(qint64 modId, const QString& name, int folderCount);
    void checkingUpdatesChanged();
    void addonUpToDate(qint64 modId, const QString& name);  // result of checkUpdate(modId)
    void updateFound(qint64 modId);                         // ... or an update was queued for it
    void linked(qint64 modId, const QString& name);    void updateCheckFinished(int available);
    void errorOccurred(const QString& context, const QString& message);
    void installFinished(qint64 modId, const QString& displayName);
    void updateApplied(qint64 modId, const QString& displayName);
    void downloadBlocked(qint64 modId, const QString& modName, const QString& modSlug,
                         qint64 fileId, const QString& fileName);

private:
    void dispatchApply(qint64 modId, qint64 fileId);
    void fetchInstallPage();
    void fetchInstallPageIfNeeded();

    QThread thread_;
    WamWorker* worker_; // lives on thread_; never call its methods directly

    SearchResultsModel searchResults_;
    InstalledAddonsModel installedAddons_;
    PendingUpdatesModel pendingUpdates_;
    ScanResultsModel scanResults_;
    InstallFilesModel installFiles_;
    QSet<qint64> singleChecks_; // addons whose own update check is in flight
    int autoInstallFetches_ = 0; // pages fetched in a row without the user asking, capped
    FlavorsModel flavorEntries_;
    QHash<qint64, qint64> installFlavors_; // flavor picked per install, reused by the blocked/manual flow

    bool hasApiKey_ = false;
    bool hasWowPath_ = false;
    bool checking_ = false;
    bool scanning_ = false;
    QString wowPath_;
    qint64 wowFlavorId_ = 0;
    QString wowFlavorName_;
};

} // namespace wam::gui
