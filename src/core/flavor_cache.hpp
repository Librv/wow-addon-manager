#pragma once
#include "core/curseforge_client.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wam {

// One WoW flavor as CurseForge defines it, plus the name the user wants to see.
struct FlavorEntry {
    std::string slug;    // CurseForge's key for it, e.g. "wow-forever"
    int64_t id = 0;      // CurseForge's gameVersionTypeId, what the API filters by
    std::string name;    // display name: CurseForge's own unless the user edited it
    std::string apiName; // CurseForge's own name, to tell an edit from a rename upstream
};

// The flavors CurseForge defines for WoW, kept on disk so the app knows them
// before (and without) the network, and so the user can rename them.
//
// flavors.json sits next to config.json and maps each CurseForge key to its
// entry:  { "wow-forever": { "id": 12345, "name": "Forever", "api_name": "Forever" } }
// The app refreshes it from CurseForge at startup (merge()); the user edits
// display names in Settings (rename()) or by hand.
class FlavorCache {
public:
    static std::filesystem::path path();

    // An empty cache if the file is missing or unreadable. Never throws.
    static FlavorCache load();
    void save() const;

    // Folds in the live list from CurseForge: new flavors are added; a known
    // flavor gets its id and CurseForge name refreshed; its display name
    // follows CurseForge's unless the user changed it. Flavors CurseForge no
    // longer lists are kept, since installed addons may still refer to them.
    void merge(const std::vector<GameVersionType>& live);

    // Sets a display name. An empty name resets it to CurseForge's. False if
    // the key is unknown.
    bool rename(const std::string& slug, const std::string& name);

    const std::vector<FlavorEntry>& all() const { return entries_; }
    std::optional<FlavorEntry> findById(int64_t id) const;
    // The display name for an id, or `fallback` if it is not in the cache.
    std::string nameFor(int64_t id, const std::string& fallback = {}) const;
    // The entries as GameVersionTypes, for matching. name is the display name,
    // or CurseForge's own name if apiNames is true.
    std::vector<GameVersionType> asTypes(bool apiNames = false) const;

    // The key used for a flavor: CurseForge's slug, or a slug made from its
    // name when CurseForge gives none.
    static std::string keyFor(const GameVersionType& t);

private:
    std::vector<FlavorEntry> entries_;
};

} // namespace wam
