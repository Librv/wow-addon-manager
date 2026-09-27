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
};

// installed.json lives at Config::dataDir()/installed.json — this is the
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
