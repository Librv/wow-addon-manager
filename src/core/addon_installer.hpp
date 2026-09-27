#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace wam {

class AddonInstaller {
public:
    // Extracts the addon zip into addonsDir (Interface/AddOns). Returns the
    // set of top-level folder names written — these are what CurseForge
    // calls "modules" and are what a later update/remove needs to touch.
    // Throws std::runtime_error on any zip or filesystem error.
    static std::vector<std::string> extractZip(const std::filesystem::path& zipPath,
                                                const std::filesystem::path& addonsDir);

    // Deletes the given top-level folders from addonsDir. Missing folders
    // are silently skipped (already-gone is not an error here).
    static void removeFolders(const std::filesystem::path& addonsDir,
                               const std::vector<std::string>& folders);
};

} // namespace wam
