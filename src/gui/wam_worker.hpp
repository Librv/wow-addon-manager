#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <optional>
#include "core/config.hpp"
#include "core/curseforge_client.hpp"
#include "core/state_store.hpp"
#include "gui/scan_results_model.hpp"
#include <QStringList>

namespace wam::gui {

// Runs on its own QThread (moved there by WamController, never constructed
// with a thread-affine parent). Owns the only live wam_core state (Config,
// StateStore, CurseForgeClient) in the GUI process. The GUI thread never
// touches these directly, only the copies pushed out through signals. Every
// slot below does blocking network or disk I/O and must only ever be invoked
// via QMetaObject::invokeMethod(worker, "slotName", Qt::QueuedConnection,
// ...) from the GUI thread; a direct call would just run wam_core's blocking
// calls on the caller's own thread, defeating the whole point.
//
// Flavors are always passed as CurseForge gameVersionTypeIds (0 = none).
class WamWorker : public QObject {
    Q_OBJECT
public:
    explicit WamWorker(QObject* parent = nullptr);

public slots:
    // Loads Config + StateStore, builds a CurseForgeClient if a key is
    // configured, and recovers any install a previous run left half-done.
    // Always emits configChanged + addonListLoaded, even with an empty
    // config, so the UI has something to render on first paint.
    void initialize();

    void setApiKey(const QString& key);
    // Saves the path and works out which flavor the folder is for (see
    // CurseForgeClient::matchFlavorForFolder). The previous flavor belonged to
    // the previous folder, so it is cleared first.
    void setWowPath(const QString& path);
    // Corrects the flavor of the WoW folder by hand (0 clears it).
    void setWowFlavor(qint64 flavorTypeId);

    void search(const QString& query);
    void listGameVersionTypes();
    void getFiles(qint64 modId, qint64 flavorTypeId);

    // One page of a mod's files for a flavor, for the install dialog. Emits
    // installFilesLoaded or installFilesFailed.
    void loadInstallFiles(qint64 modId, qint64 flavorTypeId, int index);

    // Installs one specific file (the version picked in the install dialog).
    // flavorTypeId is recorded on the addon and only affects this addon.
    void installFile(qint64 modId, qint64 fileId, qint64 flavorTypeId);

    // Installs from a zip the user downloaded themselves (author-blocked
    // downloads). Replaces the tracked version if there is one. A
    // flavorTypeId of 0 keeps whatever flavor is already recorded.
    void installManual(qint64 modId, qint64 fileId, const QString& zipPath, qint64 flavorTypeId);

    // Latest-vs-installed for one tracked mod, within the flavor it was
    // installed for. Emits updateAvailable / upToDate / errorOccurred.
    // Downloads and modifies nothing; that is applyUpdate's job.
    void checkUpdate(qint64 modId, const QString& channel);

    // checkUpdate for every CurseForge-sourced tracked addon, then
    // allUpdatesChecked.
    void checkAllUpdates();

    // Downloads and swaps in the given file (transactionally: a failure
    // leaves the installed version untouched). Re-fetches the file by id
    // first, since the files list can move between check and apply.
    void applyUpdate(qint64 modId, qint64 fileId);

    // Records which flavor a tracked addon is for (adopted/legacy addons).
    void setFlavor(qint64 modId, qint64 flavorTypeId);

    // Lists AddOns folders wam doesn't track yet (pure read; looks up mod
    // names/icons for .toc-tagged ones when a key is set). Emits scanFinished.
    void scan();

    // Adopts folders under modId (see Reconciler::adopt). includeSiblings also
    // claims the mod's other untracked modules, found via the API's file
    // moduleNames, since most multi-module addons only tag their primary
    // module. rescan re-emits scanFinished afterwards.
    void adopt(qint64 modId, const QStringList& folders, bool includeSiblings, bool rescan);

    // Fills in missing addon icons for tracked addons (one batched request per
    // 50 mods). Silent and best-effort: a failure just leaves the placeholder.
    void backfillIcons();

    void refreshAddonList();
    void removeAddon(qint64 modId);
    void untrackAddon(qint64 modId);

signals:
    void configChanged(bool hasApiKey, bool hasWowPath, const QString& wowPath,
                       qint64 wowFlavorId, const QString& wowFlavorName);
    void errorOccurred(const QString& context, const QString& message);

    void searchFinished(const QString& query, const QList<wam::CurseForgeMod>& results);
    void gameVersionTypesLoaded(const QList<wam::GameVersionType>& types);
    void filesLoaded(qint64 modId, const QList<wam::CurseForgeFile>& files);
    void installFilesLoaded(qint64 modId, qint64 flavorTypeId, int index,
                            const QList<wam::CurseForgeFile>& files, int totalCount);
    void installFilesFailed(qint64 modId, qint64 flavorTypeId, const QString& message);

    void installFinished(const wam::InstalledAddon& addon);
    // Author blocked third-party downloads: the UI should offer the manual
    // flow (open the CurseForge page, then installManual) rather than retry.
    void downloadBlocked(qint64 modId, const QString& modName, const QString& modSlug,
                         qint64 fileId, const QString& fileName);

    // currentFile.id == 0 means "adopted, never pinned": render as unknown.
    // Also fires for author-blocked files (latestFile.isBlocked()).
    void updateAvailable(qint64 modId, const QString& modName, const QString& modSlug,
                         const QString& iconUrl, const QString& flavorName,
                         const wam::CurseForgeFile& currentFile, const wam::CurseForgeFile& latestFile);
    void upToDate(qint64 modId, const QString& modName);
    void updateApplied(const wam::InstalledAddon& addon);
    void updateFailed(qint64 modId, const QString& message);
    void allUpdatesChecked();

    void scanFinished(const QList<wam::gui::ScanGroup>& groups);
    void adopted(qint64 modId, const QString& name, int folderCount);

    void addonListLoaded(const QList<wam::InstalledAddon>& addons);
    void addonRemoved(qint64 modId);
    void addonUntracked(qint64 modId);

private:
    // Returns the client, or emits errorOccurred(context, ...) and returns
    // nullptr if no key is set. A pointer, not a copy: the client caches
    // flavor and class ids, and a copy would throw that cache away.
    wam::CurseForgeClient* requireClient(const char* context);

    void emitConfig();
    // If a key and a WoW folder are set but no flavor is known yet, tries to
    // work it out from the folder name. Best effort: needs the flavor list.
    void detectFlavor();

    wam::Config config_;
    std::optional<wam::CurseForgeClient> client_;
    wam::StateStore state_;
};

} // namespace wam::gui
