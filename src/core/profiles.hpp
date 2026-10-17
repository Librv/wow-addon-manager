#pragma once
#include "core/state_store.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wam {

// A profile is a named set of addons for one WoW folder and flavor, like a
// Minecraft modpack. Each one has its own installed.json with pinned file
// ids. At most one profile is active per WoW folder: that is the one whose
// addons are in the folder's AddOns/. The others can still be edited; their
// installs and updates only go into the AddonLibrary.
// See docs/decisions/0008-profiles-share-a-zip-library.md.
struct Profile {
    int64_t id = 0;                         // stable; names the profiles/<id>/ folder
    std::string name;
    bool autoNamed = false;                 // name follows the flavor until the user renames it
    std::optional<std::string> wowPath;     // the WoW folder (e.g. .../_retail_); unset = not chosen yet
    std::optional<int64_t> flavorId;        // CurseForge gameVersionTypeId of that folder; unset = unknown
    std::optional<std::string> flavorName;
    bool active = false;                    // its addons are what is in <wowPath>/Interface/AddOns

    // <wowPath>/Interface/AddOns, throws if wowPath is unset.
    std::filesystem::path addonsDir() const;
    // The last component of wowPath ("_retail_"), empty if unset.
    std::string wowFolderName() const;
};

// Downloaded and snapshotted addon zips, shared by all profiles, under
// Config::dataDir()/library/. Switching profiles and editing inactive ones
// reads from here, so nothing is downloaded twice. Zips are kept until no
// profile refers to them (collectGarbage), since a blocked download cannot
// be fetched again.
class AddonLibrary {
public:
    static std::filesystem::path dir();

    // Where the zip for an addon in a profile lives:
    //   fileId != 0: library/<modId>/<fileId>.zip (shared between profiles)
    //   fileId == 0 (adopted): library/<modId>/adopted-<profileId>.zip
    static std::filesystem::path zipFor(int64_t profileId, const InstalledAddon& addon);

    // Copies zip into the library at `dest` (atomically: temp file, rename).
    // A zip already there is kept as is, unless `replace` (an adopted
    // addon's own copy). Returns dest.
    static std::filesystem::path store(const std::filesystem::path& zip,
                                       const std::filesystem::path& dest,
                                       bool replace = false);

    // Packs the given top-level folders of addonsDir into a zip at `dest`,
    // for addons whose zip is not in the library yet (adopted, or installed
    // before profiles existed). Same folder name guards as installZip.
    static void snapshot(const std::filesystem::path& addonsDir,
                         const std::vector<std::string>& folders,
                         const std::filesystem::path& dest);

    // Deletes library zips that no profile's installed.json (listed or
    // not) and no pending switch.json refers to. Does nothing if any
    // profile's installed.json cannot be read, since its zips are unknown.
    static void collectGarbage(const class ProfileStore& profiles);
};

// profiles.json (next to installed.json in Config::dataDir()): the list of
// profiles and which one the front ends show. Each profile's addons are in
// profiles/<id>/installed.json.
class ProfileStore {
public:
    // First run with no profiles.json: makes one profile from config.json's
    // wow_path and flavor, copies installed.json into it (the original is
    // left in place), marks it active if it has a folder, and names it after
    // the flavor, or "Default" if that is unknown. There is always at least
    // one profile after load().
    //
    // A damaged profiles.json is moved aside. If profiles/<id>/ folders
    // exist, each becomes an inactive profile "Recovered <id>" with no
    // folder; only with none does it migrate from config.json again.
    static ProfileStore load();
    void save() const; // atomic; every change below also saves at once

    const std::vector<Profile>& all() const { return profiles_; }
    const Profile* find(int64_t id) const;

    // The profile the front ends show and act on (the GUI's current tab, the
    // CLI's default). Always a valid id after load().
    int64_t selectedId() const { return selected_; }
    const Profile& selected() const;
    void select(int64_t id); // throws on an unknown id

    // A new, inactive profile with the selected profile's folder and flavor
    // and no addons. Returns its id.
    int64_t create(const std::string& name);
    void rename(int64_t id, const std::string& name); // clears autoNamed
    // Throws for the active profile of a folder, the last profile left, or
    // one in an unfinished switch. Deletes profiles/<id>/ and then collects
    // library garbage.
    void remove(int64_t id);

    // Points a profile at another WoW folder, clearing its flavor (it
    // described the old folder). Nothing on disk is touched. If no other
    // profile is active on the new folder, an active profile stays active
    // (the game folder moved), and an inactive one with no addons becomes
    // active, since its empty list matches a folder nobody manages.
    // Otherwise it is inactive there. The same folder is a no-op.
    void setWowPath(int64_t id, const std::string& path);
    void setFlavor(int64_t id, std::optional<int64_t> flavorId, std::optional<std::string> flavorName);

    // The active profile on the same WoW folder as `path`, if any. Paths are
    // compared after weakly_canonical (so symlinks match; lexically_normal
    // if that fails) and trailing-slash trimming.
    const Profile* activeFor(const std::string& wowPath) const;

    // profiles/<id>/installed.json, for StateStore::load(path).
    std::filesystem::path statePath(int64_t id) const;

private:
    friend class ProfileOps; // activate and recoverSwitch flip the active flags
    Profile* findMutable(int64_t id);

    std::vector<Profile> profiles_;
    int64_t selected_ = 0;
    int64_t nextId_ = 1;
    std::filesystem::path path_;
};

// Installs and removals for one profile. These are the only calls front
// ends use for addons in a profile; they in turn are the only callers of
// installZip for profiles (rule 4 still holds).
class ProfileOps {
public:
    // Stores zip in the library under zipFor(profile.id, addon), then, if
    // the profile is active, installs it with installZip(replaceFolders).
    // An inactive profile's AddOns/ is never touched; the folder set is read
    // from the zip with AddonInstaller::listZipFolders. Returns the folder
    // set to record on the addon. `addon` must already carry the new
    // modId/fileId.
    static std::vector<std::string> placeZip(const Profile& profile,
                                             const InstalledAddon& addon,
                                             const std::filesystem::path& zip,
                                             const std::vector<std::string>& replaceFolders);

    // Removes the addon's folders from AddOns/ if the profile is active;
    // for an inactive profile nothing on disk changes. The caller drops it
    // from the profile's StateStore and then calls collectGarbage.
    static void removeAddon(const Profile& profile, const InstalledAddon& addon);

    // Fetches a library zip that is missing (deleted by hand, or never
    // stored) into `dest`. Returns false if it cannot (no API key, blocked
    // download); activate then fails before anything on disk changes.
    using FetchFn = std::function<bool(const InstalledAddon& addon, const std::filesystem::path& dest)>;

    // Makes `targetId` the active profile on its WoW folder:
    //  1. snapshots the current active profile's addons that have no
    //     library zip yet (adopted ones, or installs from before profiles);
    //  2. makes sure every target zip is in the library (fetch if missing);
    //  3. writes profiles/switch.json, the plan;
    //  4. removes addons only the old profile has, installs addons only the
    //     target has or has at another fileId (installZip from the library),
    //     and leaves addons with the same modId and fileId alone;
    //  5. flips the active flags, saves, deletes switch.json.
    // Untracked folders in AddOns/ are left alone unless a zip overwrites
    // them, as with any install. Updates the target's installed.json with
    // the folder sets installZip returned. Throws before step 3 without
    // changing anything in AddOns/ (step 2 reads every zip in full).
    // If step 4 fails, it rolls back, touching only what the plan reached
    // (progress is kept in switch.json): removes folders of started steps,
    // restores untracked folders they overwrote from profiles/switch-backup/,
    // reinstalls the old addons it touched from the library, deletes
    // switch.json and rethrows; the old profile stays active. Only if that
    // rollback fails too is switch.json left for recoverSwitch.
    static void activate(ProfileStore& profiles, int64_t targetId, const FetchFn& fetch);

    // If a previous activate was interrupted (switch.json exists), runs the
    // rest of its plan again (every step is safe to repeat) and finishes the
    // switch, rolling back as activate does if that fails. Front ends call
    // this at startup, after AddonInstaller::recoverInterrupted. Returns
    // true if it finished one.
    static bool recoverSwitch(ProfileStore& profiles);

    // Test seams. failSwitchAfterInstallsForTesting >= 0: step 4 throws
    // after that many installs (a real failure, rolled back).
    // crashSwitchAfterInstallsForTesting >= 0: same, but as if the process
    // died: no rollback, switch.json stays.
    static inline int failSwitchAfterInstallsForTesting = -1;
    static inline int crashSwitchAfterInstallsForTesting = -1;
};

} // namespace wam
