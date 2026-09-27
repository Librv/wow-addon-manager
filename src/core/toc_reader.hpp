#pragma once
#include <string>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace wam {

// Metadata read out of a WoW addon's .toc file(s).
//
// Addon authors' packaging tools (e.g. the BigWigsMods packager) stamp
// provider ids into the .toc as "## Key: Value" comment lines at release
// time — this is baked into the addon's own files, not something the
// downloading client writes after the fact. It's the same heuristic real
// addon managers (WowUp, wocli, ...) use to recognize a pre-existing
// install: X-Curse-Project-ID identifies the CurseForge mod (not the exact
// file/version — there's no per-version tag), and WoWI/Wago id are stashed
// here now, unused until later phases add those sources.
struct TocMetadata {
    std::optional<int64_t> curseProjectId;
    std::optional<std::string> wowiId;
    std::optional<std::string> wagoId;
    std::optional<std::string> version;
    std::optional<std::string> title;

    bool empty() const {
        return !curseProjectId && !wowiId && !wagoId && !version && !title;
    }
};

class TocReader {
public:
    // Reads every *.toc directly inside folderPath (a folder can have more
    // than one for flavor-specific variants, e.g. Foo.toc / Foo_Vanilla.toc)
    // and merges them: first non-empty value found for each field wins.
    // Returns a default (all-nullopt) TocMetadata if the folder has no .toc
    // at all, or doesn't exist.
    static TocMetadata readFolder(const std::filesystem::path& folderPath);

private:
    static TocMetadata parseFile(const std::filesystem::path& tocPath);
};

} // namespace wam
