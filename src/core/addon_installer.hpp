#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace wam {

class AddonInstaller {
public:
    // Extracts the addon zip into destDir. Returns the set of top-level
    // folder names written; these are what CurseForge calls "modules".
    // Rejects entries that could escape destDir (".." components or absolute
    // paths) and fails on truncated reads/writes. Throws std::runtime_error
    // on any zip or filesystem error. This is a raw extraction: it is not
    // transactional, and a failure can leave partial files in destDir. Real
    // installs go through installZip() below.
    static std::vector<std::string> extractZip(const std::filesystem::path& zipPath,
                                                const std::filesystem::path& destDir);

    // Transactional install/update. The zip is fully extracted into a staging
    // folder next to the addons (<addonsDir>/.wam-staging) first; if that
    // fails, addonsDir has not been touched. Only then are the old folders
    // (replaceFolders, plus any existing folder the zip is about to overwrite)
    // moved into <addonsDir>/.wam-backup and the staged folders renamed into
    // place. Any failure during the swap rolls everything back to the exact
    // previous state. Staging, backup and addonsDir share a filesystem, so
    // the swap is plain renames.
    //
    // replaceFolders: the folders the previous version owned (empty for a
    // fresh install). Folders that version had but the new zip no longer
    // ships are removed once the swap commits.
    //
    // Returns the new top-level folder set. Throws std::runtime_error, and
    // refuses zips with no addon folders at all.
    static std::vector<std::string> installZip(const std::filesystem::path& zipPath,
                                                const std::filesystem::path& addonsDir,
                                                const std::vector<std::string>& replaceFolders = {});

    // If a previous installZip was killed mid-swap (crash, power loss, kill
    // -9), .wam-backup still holds the pre-swap folders. This puts them back
    // and clears leftover staging/trash. Returns how many folders were
    // restored. installZip calls this first; front-ends also call it at
    // startup. Throws if a folder cannot be restored (the backup is kept).
    static std::size_t recoverInterrupted(const std::filesystem::path& addonsDir);

    // Deletes the given top-level folders from addonsDir. Missing folders
    // are silently skipped (already-gone is not an error here). Names that
    // are not a single plain path component (e.g. "../x") are skipped.
    static void removeFolders(const std::filesystem::path& addonsDir,
                               const std::vector<std::string>& folders);

    // Test seam: when >= 0, installZip throws after that many staged folders
    // have been renamed into place, to exercise rollback deterministically.
    static inline int failAfterInstalledForTesting = -1;
};

} // namespace wam
