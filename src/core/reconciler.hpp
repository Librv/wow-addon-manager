#pragma once
#include "core/state_store.hpp"
#include "core/toc_reader.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace wam {

// One top-level AddOns/ folder wam doesn't yet track, and what its .toc
// claims about it (if anything).
struct ScanEntry {
    std::string folder;
    TocMetadata toc;
};

class Reconciler {
public:
    // Lists every top-level folder in addonsDir that isn't already covered
    // by state (i.e. not present in any InstalledAddon::folders), with its
    // .toc metadata read. Pure read: does not touch StateStore. wam's own
    // transient install folders (".wam-*", see AddonInstaller) are never
    // reported.
    static std::vector<ScanEntry> scan(const std::filesystem::path& addonsDir,
                                        const StateStore& state);

    // Groups scan() results by X-Curse-Project-ID. Entries with no id at
    // all are returned separately (key 0 is never a real CurseForge mod id).
    static std::vector<std::pair<int64_t, std::vector<ScanEntry>>> groupByModId(
        const std::vector<ScanEntry>& entries);

    // Records the given AddOns folders as belonging to modId, merging into an
    // existing entry for that mod if there is one (a new entry gets
    // fileId = 0, "version unknown"). All-or-nothing: every folder is
    // validated first and `state` is only modified if they all pass. Throws
    // std::runtime_error if a folder name is not a plain single component, is
    // missing from addonsDir, or is already tracked under a *different* mod.
    // Does not call state.save(); that is the caller's decision.
    static InstalledAddon adopt(StateStore& state, const std::filesystem::path& addonsDir,
                                int64_t modId, const std::string& displayName,
                                const std::string& iconUrl,
                                const std::vector<std::string>& folders);
};

} // namespace wam
