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
    // .toc metadata read. Pure read — does not touch StateStore.
    static std::vector<ScanEntry> scan(const std::filesystem::path& addonsDir,
                                        const StateStore& state);

    // Groups scan() results by X-Curse-Project-ID. Entries with no id at
    // all are returned separately (key 0 is never a real CurseForge mod id).
    static std::vector<std::pair<int64_t, std::vector<ScanEntry>>> groupByModId(
        const std::vector<ScanEntry>& entries);
};

} // namespace wam
