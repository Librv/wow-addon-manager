#include "core/addon_installer.hpp"
#include <zip.h>
#include <fstream>
#include <stdexcept>
#include <set>
#include <vector>

namespace wam {

namespace fs = std::filesystem;

namespace {

std::string topLevelFolder(const std::string& entryName) {
    auto pos = entryName.find('/');
    if (pos == std::string::npos) return {}; // file at zip root, no folder — ignore
    return entryName.substr(0, pos);
}

} // namespace

std::vector<std::string> AddonInstaller::extractZip(const fs::path& zipPath, const fs::path& addonsDir) {
    int errCode = 0;
    zip_t* archive = zip_open(zipPath.string().c_str(), ZIP_RDONLY, &errCode);
    if (!archive) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errCode);
        std::string msg = zip_error_strerror(&ze);
        zip_error_fini(&ze);
        throw std::runtime_error("failed to open addon zip '" + zipPath.string() + "': " + msg);
    }

    fs::create_directories(addonsDir);
    std::set<std::string> folders;

    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        const char* rawName = zip_get_name(archive, i, 0);
        if (!rawName) continue;
        std::string name(rawName);
        if (name.empty()) continue;

        // Guard against zip-slip: reject any entry that escapes addonsDir.
        if (name.find("..") != std::string::npos) {
            zip_close(archive);
            throw std::runtime_error("addon zip contains unsafe path entry: " + name);
        }

        fs::path destPath = addonsDir / name;
        bool isDir = !name.empty() && name.back() == '/';

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
        zip_fclose(zf);

        auto top = topLevelFolder(name);
        if (!top.empty()) folders.insert(top);
    }

    zip_close(archive);
    return std::vector<std::string>(folders.begin(), folders.end());
}

void AddonInstaller::removeFolders(const fs::path& addonsDir, const std::vector<std::string>& folders) {
    for (const auto& folder : folders) {
        std::error_code ec;
        fs::remove_all(addonsDir / folder, ec);
        // ec ignored: a missing folder is not a failure here.
    }
}

} // namespace wam
