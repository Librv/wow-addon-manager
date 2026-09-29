#pragma once
#include <QObject>
#include <QThread>
#include <QHash>
#include <QUrl>
#include <QVariantList>
#include "gui/wam_worker.hpp"
#include "gui/installed_addons_model.hpp"
#include "gui/search_results_model.hpp"
#include "gui/pending_updates_model.hpp"
#include "gui/scan_results_model.hpp"
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
    Q_PROPERTY(wam::gui::SearchResultsModel* searchResults READ searchResults CONSTANT)
    Q_PROPERTY(wam::gui::InstalledAddonsModel* installedAddons READ installedAddons CONSTANT)
    Q_PROPERTY(wam::gui::PendingUpdatesModel* pendingUpdates READ pendingUpdates CONSTANT)
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
    bool checkingUpdates() const { return checking_; }
    SearchResultsModel* searchResults() { return &searchResults_; }
    InstalledAddonsModel* installedAddons() { return &installedAddons_; }
    PendingUpdatesModel* pendingUpdates() { return &pendingUpdates_; }
    ScanResultsModel* scanResults() { return &scanResults_; }
    bool scanning() const { return scanning_; }
    QVariantList flavors() const;

    // Flavors this mod has files for (all flavors if it reports none).
    // Uses cached data only, never blocks.
    Q_INVOKABLE QVariantList flavorsForMod(qint64 modId) const;
    Q_INVOKABLE QString localPath(const QUrl& url) const { return url.toLocalFile(); }

public slots:
    void setApiKey(const QString& key);
    void setWowPath(const QString& path);
    void search(const QString& query);
    void install(qint64 modId, const QString& channel, qint64 flavorTypeId);
    void installManual(qint64 modId, qint64 fileId, const QString& zipPath);
    void setFlavor(qint64 modId, qint64 flavorTypeId);

    void checkAllUpdates();
    void applyUpdate(qint64 modId);   // applies one; its row leaves the queue when done
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
    void updateCheckFinished(int available);
    void errorOccurred(const QString& context, const QString& message);
    void installFinished(qint64 modId, const QString& displayName);
    void updateApplied(qint64 modId, const QString& displayName);
    void downloadBlocked(qint64 modId, const QString& modName, const QString& modSlug,
                         qint64 fileId, const QString& fileName);

private:
    void dispatchApply(qint64 modId, qint64 fileId);

    QThread thread_;
    WamWorker* worker_; // lives on thread_; never call its methods directly

    SearchResultsModel searchResults_;
    InstalledAddonsModel installedAddons_;
    PendingUpdatesModel pendingUpdates_;
    ScanResultsModel scanResults_;
    QList<wam::GameVersionType> flavorTypes_;
    QHash<qint64, qint64> installFlavors_; // flavor picked per install, reused by the blocked/manual flow

    bool hasApiKey_ = false;
    bool hasWowPath_ = false;
    bool checking_ = false;
    bool scanning_ = false;
    QString wowPath_;
};

} // namespace wam::gui
