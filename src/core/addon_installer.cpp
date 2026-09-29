#include "core/addon_installer.hpp"
#include <zip.h>
#include <fstream>
#include <stdexcept>
#include <set>
#include <vector>

namespace wam {

namespace fs = std::filesystem;

namespace {

constexpr const char* kStagingName = ".wam-staging";
constexpr const char* kBackupName = ".wam-backup";
constexpr const char* kTrashName = ".wam-trash";

std::string topLevelFolder(const std::string& entryName) {
    auto pos = entryName.find('/');
    if (pos == std::string::npos) return {}; // file at zip root, no folder: ignore
    return entryName.substr(0, pos);
}

// A folder name we are willing to join onto addonsDir and rename/delete:
// exactly one plain path component, and never one of our own ".wam-" dirs.
bool isSafeFolderName(const std::string& name) {
    return !name.empty() && name != "." &&
           name.find("..") == std::string::npos &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           name.rfind(".wam-", 0) != 0;
}

} // namespace

std::vector<std::string> AddonInstaller::extractZip(const fs::path& zipPath, const fs::path& destDir) {
    int errCode = 0;
    zip_t* archive = zip_open(zipPath.string().c_str(), ZIP_RDONLY, &errCode);
    if (!archive) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        throw std::runtime_error("failed to open addon zip '" + zipPath.string() + "': " + msg);
    }

    fs::create_directories(destDir);
    std::set<std::string> folders;

    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        const char* rawName = zip_get_name(archive, i, 0);
        if (!rawName) continue;
        std::string name(rawName);
        if (name.empty()) continue;

        // Guard against zip-slip: reject any entry that escapes destDir,
        // either via ".." or by being an absolute path (path / "/abs"
        // discards the left-hand side).
        if (name.find("..") != std::string::npos || name.front() == '/') {
            zip_close(archive);
            throw std::runtime_error("addon zip contains unsafe path entry: " + name);
        }

        fs::path destPath = destDir / name;
        bool isDir = name.back() == '/';

        if (isDir) {
            fs::create_directories(destPath);
            continue;
        }

        fs::create_directories(destPath.parent_path());

        zip_file_t* zf = zip_fopen_index(archive, i, 0);
        if (!zf) {
            zip_close(archive);
            throw std::runtime_error("failed to read zip entry: " + name);
        }

        std::ofstream out(destPath, std::ios::binary);
        if (!out) {
            zip_fclose(zf);
            zip_close(archive);
            throw std::runtime_error("failed to write file: " + destPath.string());
        }

        char buf[65536];
        zip_int64_t n;
        while ((n = zip_fread(zf, buf, sizeof(buf))) > 0) {
            out.write(buf, static_cast<std::streamsize>(n));
        }
        const bool readFailed = n < 0; // a short/corrupt read must not pass silently
        zip_fclose(zf);
        out.close();
        if (readFailed || !out) {
            zip_close(archive);
            throw std::runtime_error("failed while extracting zip entry: " + name);
        }

        auto top = topLevelFolder(name);
        if (!top.empty()) folders.insert(top);
    }

    zip_close(archive);
    return std::vector<std::string>(folders.begin(), folders.end());
}

std::size_t AddonInstaller::recoverInterrupted(const fs::path& addonsDir) {
    std::error_code ec;
    std::size_t restored = 0;

    const fs::path backup = addonsDir / kBackupName;
    if (fs::exists(backup, ec)) {
        // Interrupted mid-swap: every folder that was touched sits in backup,
        // untouched. Restore each one over whatever partial state replaced it.
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(backup, ec))
            names.push_back(entry.path().filename().string());

        for (const auto& name : names) {
            if (!isSafeFolderName(name)) continue;
            const fs::path target = addonsDir / name;
            fs::remove_all(target, ec);
            fs::rename(backup / name, target, ec);
            if (ec) {
                throw std::runtime_error("could not restore '" + name + "' from " + backup.string() +
                                         ": " + ec.message());
            }
            ++restored;
        }
        fs::remove_all(backup, ec);
    }

    // Staging holds only not-yet-installed new files; trash holds only
    // already-superseded old ones. Neither is worth keeping.
    fs::remove_all(addonsDir / kStagingName, ec);
    fs::remove_all(addonsDir / kTrashName, ec);
    return restored;
}

std::vector<std::string> AddonInstaller::installZip(const fs::path& zipPath,
                                                     const fs::path& addonsDir,
                                                     const std::vector<std::string>& replaceFolders) {
    for (const auto& f : replaceFolders)
        if (!isSafeFolderName(f))
            throw std::runtime_error("refusing to replace unsafe folder name: '" + f + "'");

    fs::create_directories(addonsDir);
    recoverInterrupted(addonsDir);

    const fs::path staging = addonsDir / kStagingName;
    const fs::path backup = addonsDir / kBackupName;
    const fs::path trash = addonsDir / kTrashName;
    std::error_code ec;

    // 1. Stage. Nothing in addonsDir is touched until this fully succeeds.
    fs::create_directories(staging);
    std::vector<std::string> newFolders;
    try {
        newFolders = extractZip(zipPath, staging);
        if (newFolders.empty())
            throw std::runtime_error("addon zip contains no addon folders");
        for (const auto& f : newFolders)
            if (!isSafeFolderName(f))
                throw std::runtime_error("addon zip contains an unsafe top-level folder name: '" + f + "'");
    } catch (...) {
        fs::remove_all(staging, ec);
        throw;
    }

    // 2. Work out what has to move out of the way: the previous version's
    // folders, plus anything already on disk the new folders would land on.
    std::set<std::string> touched(replaceFolders.begin(), replaceFolders.end());
    touched.insert(newFolders.begin(), newFolders.end());
    std::vector<std::string> toBackup;
    for (const auto& f : touched)
        if (fs::exists(fs::symlink_status(addonsDir / f, ec))) toBackup.push_back(f);

    // 3. Swap, with rollback on any failure.
    std::vector<std::string> movedToBackup, installed;
    auto rollback = [&]() -> bool {
        std::error_code e;
        bool ok = true;
        for (auto it = installed.rbegin(); it != installed.rend(); ++it)
            fs::remove_all(addonsDir / *it, e);
        for (const auto& f : movedToBackup) {
            fs::rename(backup / f, addonsDir / f, e);
            if (e) ok = false;
        }
        return ok;
    };

    try {
        fs::create_directories(backup);
        for (const auto& f : toBackup) {
            fs::rename(addonsDir / f, backup / f);
            movedToBackup.push_back(f);
        }
        for (const auto& f : newFolders) {
            if (failAfterInstalledForTesting >= 0 &&
                static_cast<int>(installed.size()) >= failAfterInstalledForTesting)
                throw std::runtime_error("injected failure");
            fs::rename(staging / f, addonsDir / f);
            installed.push_back(f);
        }
    } catch (const std::exception& e) {
        const bool restored = rollback();
        fs::remove_all(staging, ec);
        if (!restored) {
            // Keep the backup: recoverInterrupted() will retry the restore.
            throw std::runtime_error(std::string("install failed (") + e.what() +
                                     ") and rollback was incomplete; previous folders are preserved in " +
                                     backup.string());
        }
        fs::remove_all(backup, ec);
        throw std::runtime_error(std::string("install failed, previous state restored: ") + e.what());
    }

    // 4. Commit. Renaming backup away is the commit point: a leftover
    // .wam-backup always means "swap was interrupted, roll back", so it must
    // never linger half-deleted after a swap that succeeded.
    fs::rename(backup, trash, ec);
    if (ec) fs::remove_all(backup, ec);
    else fs::remove_all(trash, ec);
    fs::remove_all(staging, ec);
    return newFolders;
}

void AddonInstaller::removeFolders(const fs::path& addonsDir, const std::vector<std::string>& folders) {
    for (const auto& folder : folders) {
        if (!isSafeFolderName(folder)) continue; // never follow a bad name out of addonsDir
        std::error_code ec;
        fs::remove_all(addonsDir / folder, ec);
        // ec ignored: a missing folder is not a failure here.
    }
}

} // namespace wam
