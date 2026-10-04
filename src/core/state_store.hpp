#pragma once
#include "core/curseforge_client.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace wam {

// UTC timestamp like "2026-09-26T21:00:00Z", for InstalledAddon::installedAt.
std::string nowIso8601();

// One addon this app knows it installed (or was pointed at manually).
// modId == 0 means "not from CurseForge" (reserved for later sources).
struct InstalledAddon {
    int64_t modId = 0;
    int64_t fileId = 0;
    std::string displayName;
    std::string fileName;
    ReleaseChannel channel = ReleaseChannel::Release;
    std::vector<std::string> gameVersions;
    std::vector<std::string> folders; // top-level AddOns/ folders this addon owns
    std::string installedAt;          // ISO-8601 UTC
    bool manuallyProvided = false;    // true if installed via a user-supplied file (blocked download)
    int64_t flavorTypeId = 0;         // CurseForge gameVersionTypeId picked at install; 0 = unknown (adopted/legacy)
    std::string flavorName;           // display name captured at install time
    std::string iconUrl;              // CurseForge logo thumbnail; empty until known (backfilled by the GUI)
    std::string fileDisplayName;      // CurseForge's name for the installed file, e.g. "v9.3.2"; empty until known
    std::string fileDate;             // when that file was uploaded (ISO-8601); empty until known
    std::string modSlug;              // CurseForge page slug; empty until known (adopted addons have none)
    std::string author;               // CurseForge author name(s); empty until known (backfilled by the GUI)
    bool adopted = false;             // recorded from an existing folder rather than installed by wam
    int64_t downloadCount = 0;        // CurseForge's total downloads for the mod; 0 until known (refreshed at startup)
};

// Records which CurseForge file an addon is now at: id, names, upload date,
// release channel and game versions. Leaves folders, flavor and the install
// time alone.
void recordFile(InstalledAddon& addon, const CurseForgeFile& file);

// installed.json lives at Config::dataDir()/installed.json. This is the
// app's own record of what it put where, independent of the game folder
// itself. Reconciliation with what's actually on disk is milestone 2.
class StateStore {
public:
    static StateStore load();
    void save() const;

    void upsert(const InstalledAddon& addon); // keyed by modId
    bool remove(int64_t modId);               // returns true if something was removed
    std::optional<InstalledAddon> find(int64_t modId) const;
    const std::vector<InstalledAddon>& all() const { return addons_; }

private:
    std::vector<InstalledAddon> addons_;
    std::filesystem::path path_;
};

} // namespace wam
