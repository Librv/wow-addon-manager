#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <optional>
#include "core/config.hpp"
#include "core/curseforge_client.hpp"
#include "core/state_store.hpp"
#include "core/flavor_cache.hpp"
#include "core/profiles.hpp"
#include "gui/flavors_model.hpp"
#include "gui/scan_results_model.hpp"
#include <QStringList>
#include <QVariantList>

namespace wam::gui {

// Runs on its own QThread (moved there by WamController, never constructed
// with a thread-affine parent). Owns the only live wam_core state (Config,
// ProfileStore, the selected profile's StateStore, CurseForgeClient) in the
// GUI process. The GUI thread never
// touches these directly, only the copies pushed out through signals. Every
// slot below does blocking network or disk I/O and must only ever be invoked
// via QMetaObject::invokeMethod(worker, "slotName", Qt::QueuedConnection,
// ...) from the GUI thread; a direct call would just run wam_core's blocking
// calls on the caller's own thread, defeating the whole point.
//
// Flavors are always passed as CurseForge gameVersionTypeIds (0 = none).
//
// Everything below acts on the selected profile (ProfileStore::selected):
// its WoW folder, its flavor and its addon list. Installs, updates and
// removals go through ProfileOps, so on an inactive profile they only touch
// the library, never AddOns/.
class WamWorker : public QObject {
    Q_OBJECT
public:
    explicit WamWorker(QObject* parent = nullptr);

public slots:
    // Loads Config, the profiles (migrating an old installed.json on first
    // run) and the selected profile's StateStore, builds a CurseForgeClient
    // if a key is configured, and recovers any install or profile switch a
    // previous run left half-done. Always emits configChanged,
    // profilesChanged and addonListLoaded, even with an empty config, so the
    // UI has something to render on first paint.
    void initialize();

    void setApiKey(const QString& key);
    // Points the selected profile at a WoW folder and works out which flavor
    // the folder is for (see CurseForgeClient::matchFlavorForFolder). The
    // previous flavor belonged to the previous folder, so it is cleared first.
    void setWowPath(const QString& path);
    // Corrects the flavor of the selected profile's folder by hand (0 clears it).
    void setWowFlavor(qint64 flavorTypeId);

    // Profiles. Each emits profilesChanged; a change of the selected profile
    // also emits configChanged (first) and addonListLoaded.
    void selectProfile(qint64 id);
    void createProfile(const QString& name); // the new profile becomes selected
    void renameProfile(qint64 id, const QString& name);
    void deleteProfile(qint64 id);
    // Makes a profile the active one on its WoW folder (ProfileOps::activate),
    // fetching zips the library lacks by file id. Emits profileActivated on
    // success, then always activateProfileFinished.
    void activateProfile(qint64 id);

    void search(const QString& query);
    // Renames a flavor in the cache (an empty name resets it to CurseForge's).
    void renameFlavor(const QString& slug, const QString& name);
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
    // profileId is the profile the update was found for; if another one was
    // selected since, nothing is applied.
    void applyUpdate(qint64 modId, qint64 fileId, qint64 profileId);

    // Records which flavor a tracked addon is for (adopted/legacy addons).
    void setFlavor(qint64 modId, qint64 flavorTypeId);

    // Lists AddOns folders wam doesn't track yet (pure read; looks up mod
    // names/icons for .toc-tagged ones when a key is set). Emits scanFinished.
    // Refused on an inactive profile: the folder holds another profile's addons.
    void scan();

    // Adopts folders under modId (see Reconciler::adopt). includeSiblings also
    // claims the mod's other untracked modules, found via the API's file
    // moduleNames, since most multi-module addons only tag their primary
    // module. rescan re-emits scanFinished afterwards. Refused on an inactive
    // profile, or if profileId (the profile the scan was for) is no longer
    // the selected one.
    void adopt(qint64 modId, const QStringList& folders, bool includeSiblings, bool rescan, qint64 profileId);

    // Fills in what tracked addons are missing and CurseForge can tell us:
    // icons and page slugs (from the mods) and version names and upload dates
    // (from the files), in batched requests. Silent and best-effort.
    void backfillDetails();

    // checkUpdate for one addon, then singleCheckFinished.
    void checkOneUpdate(qint64 modId);

    // Records which CurseForge file an adopted addon matches. Nothing is
    // downloaded and nothing on disk changes; the flavor is the one the file
    // was picked under.
    void linkFile(qint64 modId, qint64 fileId, qint64 flavorTypeId);

    // A file's changelog as plain text, for the details panel.
    void loadChangelog(qint64 modId, qint64 fileId);

    void refreshAddonList();
    void removeAddon(qint64 modId);
    void untrackAddon(qint64 modId);

signals:
    void configChanged(bool hasApiKey, bool hasWowPath, const QString& wowPath,
                       qint64 wowFlavorId, const QString& wowFlavorName);
    void errorOccurred(const QString& context, const QString& message);

    void searchFinished(const QString& query, const QList<wam::CurseForgeMod>& results);
    void flavorsChanged(const QList<wam::gui::FlavorInfo>& flavors);
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
    void singleCheckFinished(qint64 modId);
    void linked(qint64 modId, const QString& name);
    void changelogLoaded(qint64 fileId, const QString& text);
    void changelogFailed(qint64 fileId, const QString& message);

    void scanFinished(const QList<wam::gui::ScanGroup>& groups);
    void adopted(qint64 modId, const QString& name, int folderCount);

    // One map per profile, every key always present:
    // {id, name, active, wowPath ("" if unset), flavorName ("" if unknown), addonCount}.
    void profilesChanged(const QVariantList& profiles, qint64 selectedId);
    void profileActivated(qint64 id, const QString& name);
    void activateProfileFinished();

    void addonListLoaded(const QList<wam::InstalledAddon>& addons);
    void addonRemoved(qint64 modId);
    void addonUntracked(qint64 modId);

private:
    // Returns the client, or emits errorOccurred(context, ...) and returns
    // nullptr if no key is set. A pointer, not a copy: the client caches
    // flavor and class ids, and a copy would throw that cache away.
    wam::CurseForgeClient* requireClient(const char* context);

    // The selected profile, as a copy (the store's vector moves on create), or
    // nullopt with errorOccurred(context, ...) if there is none.
    std::optional<wam::Profile> requireProfile(const char* context);
    const wam::Profile* selectedProfile() const;

    void emitConfig();
    void emitProfiles();
    void emitAddons();
    // Reloads the selected profile's installed.json into state_, reporting a
    // damaged file.
    void loadState();
    // After the selected profile changed: its state, config, profiles, addons.
    void showSelected();
    // Deletes library zips nothing refers to any more. Best effort.
    void collectGarbage();
    qint64 effectiveFlavor(const wam::InstalledAddon& a) const;
    void emitFlavors();
    // Fetches CurseForge's flavor list and folds it into the cache. Silent on
    // failure: the cache on disk keeps working.
    void refreshFlavors();
    // If a WoW folder is set but no flavor is known yet, tries to work it out
    // from the folder name and the flavor cache.
    void detectFlavor();

    wam::Config config_; // only the API key is used; folder and flavor live in the profiles
    wam::ProfileStore profiles_;
    std::optional<wam::CurseForgeClient> client_;
    wam::StateStore state_;
    wam::FlavorCache flavors_;
};

} // namespace wam::gui
