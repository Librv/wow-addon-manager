// Lightweight hand-rolled test harness — no test framework dependency,
// matching the project's lightweight-tooling preference. Exercises the
// parts of milestone 1 that don't require live network/API access:
// zip extraction, state persistence, and file-selection logic.

#include "core/addon_installer.hpp"
#include "core/state_store.hpp"
#include "core/curseforge_client.hpp"
#include "core/config.hpp"
#include "core/toc_reader.hpp"
#include "core/reconciler.hpp"

#include <zip.h>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>

using namespace wam;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    } else {
        std::cout << "ok:   " << what << "\n";
    }
}

// Builds a small synthetic addon zip at destPath containing:
//   TestAddon/TestAddon.toc
//   TestAddon/TestAddon.lua
//   TestAddon/Libs/Lib.lua
void buildSyntheticAddonZip(const fs::path& destPath) {
    int errCode = 0;
    zip_t* z = zip_open(destPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errCode);
    if (!z) throw std::runtime_error("failed to create test zip");

    // zip_source_buffer with freep=0 does NOT copy the data — libzip reads
    // it lazily, only when zip_close() actually writes the archive. The
    // backing strings must outlive zip_close(), so they're kept here rather
    // than as lambda-local temporaries.
    static std::vector<std::string> contents;
    contents = {
        "## Interface: 110000\n## Title: TestAddon\n",
        "print('hello from TestAddon')\n",
        "-- a nested library file\n",
    };
    std::vector<std::string> names = {
        "TestAddon/TestAddon.toc",
        "TestAddon/TestAddon.lua",
        "TestAddon/Libs/Lib.lua",
    };

    for (size_t i = 0; i < names.size(); ++i) {
        zip_source_t* src = zip_source_buffer(z, contents[i].data(), contents[i].size(), 0);
        zip_file_add(z, names[i].c_str(), src, ZIP_FL_ENC_UTF_8);
    }

    zip_close(z);
}

void buildZipSlipZip(const fs::path& destPath) {
    int errCode = 0;
    zip_t* z = zip_open(destPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errCode);
    if (!z) throw std::runtime_error("failed to create zip-slip test zip");

    std::string content = "malicious";
    zip_source_t* src = zip_source_buffer(z, content.data(), content.size(), 0);
    zip_file_add(z, "../../evil.lua", src, ZIP_FL_ENC_UTF_8);

    zip_close(z);
}

void testExtractZip(const fs::path& workDir) {
    auto zipPath = workDir / "TestAddon.zip";
    auto addonsDir = workDir / "AddOns";
    buildSyntheticAddonZip(zipPath);

    auto folders = AddonInstaller::extractZip(zipPath, addonsDir);

    check(folders.size() == 1 && folders[0] == "TestAddon", "extractZip returns the single top-level folder");
    check(fs::exists(addonsDir / "TestAddon" / "TestAddon.toc"), "extractZip writes the .toc file");
    check(fs::exists(addonsDir / "TestAddon" / "Libs" / "Lib.lua"), "extractZip preserves nested directories");

    std::ifstream in(addonsDir / "TestAddon" / "TestAddon.lua");
    std::string firstLine;
    std::getline(in, firstLine);
    check(firstLine == "print('hello from TestAddon')", "extracted file content matches source");

    AddonInstaller::removeFolders(addonsDir, folders);
    check(!fs::exists(addonsDir / "TestAddon"), "removeFolders deletes the installed folder");
}

void testZipSlipRejected(const fs::path& workDir) {
    auto zipPath = workDir / "evil.zip";
    auto addonsDir = workDir / "AddOns2";
    buildZipSlipZip(zipPath);

    bool threw = false;
    try {
        AddonInstaller::extractZip(zipPath, addonsDir);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "extractZip rejects zip entries containing '..'");
}

void testStateStoreRoundtrip(const fs::path& workDir) {
    setenv("XDG_DATA_HOME", workDir.string().c_str(), 1);

    InstalledAddon a;
    a.modId = 12345;
    a.fileId = 999;
    a.displayName = "TestAddon";
    a.fileName = "TestAddon-1.0.0.zip";
    a.channel = ReleaseChannel::Beta;
    a.gameVersions = {"Retail", "11.0.7"};
    a.folders = {"TestAddon"};
    a.installedAt = nowIso8601();
    a.manuallyProvided = true;

    auto store = StateStore::load();
    store.upsert(a);
    store.save();

    auto reloaded = StateStore::load();
    auto found = reloaded.find(12345);
    check(found.has_value(), "state store persists an upserted addon");
    if (found) {
        check(found->displayName == "TestAddon", "persisted displayName round-trips");
        check(found->channel == ReleaseChannel::Beta, "persisted channel round-trips");
        check(found->folders.size() == 1 && found->folders[0] == "TestAddon", "persisted folders round-trip");
        check(found->manuallyProvided == true, "persisted manuallyProvided flag round-trips");
    }

    bool removed = reloaded.remove(12345);
    reloaded.save();
    auto afterRemove = StateStore::load();
    check(removed, "remove() reports it removed an entry");
    check(!afterRemove.find(12345).has_value(), "removed addon no longer present after reload");
}

void testSelectBestFile() {
    std::vector<CurseForgeFile> files;

    CurseForgeFile f1; f1.id = 100; f1.releaseType = ReleaseChannel::Release; f1.gameVersions = {"11.0.7"};
    CurseForgeFile f2; f2.id = 200; f2.releaseType = ReleaseChannel::Release; f2.gameVersions = {"11.0.7"};
    CurseForgeFile f3; f3.id = 150; f3.releaseType = ReleaseChannel::Beta; f3.gameVersions = {"1.15.5"};
    files = {f1, f2, f3};

    auto best = CurseForgeClient::selectBestFile(files, ReleaseChannel::Release);
    check(best.has_value() && best->id == 200, "selectBestFile picks the newest matching-channel file");

    auto beta = CurseForgeClient::selectBestFile(files, ReleaseChannel::Beta);
    check(beta.has_value() && beta->id == 150, "selectBestFile filters by channel");

    auto noMatch = CurseForgeClient::selectBestFile(files, ReleaseChannel::Alpha);
    check(!noMatch.has_value(), "selectBestFile returns nullopt when no channel matches");
}

void testNoApiKeyStillUsable(const fs::path& workDir) {
    // Core requirement from the plan: the app must stay usable for managing
    // already-installed addons with no API key and no network. Config with
    // no key must not throw merely by existing / being loaded, and
    // addonsDir() must work once wow_path is set even with no key.
    setenv("XDG_CONFIG_HOME", workDir.string().c_str(), 1);
    Config cfg; // default-constructed: no key, no path
    check(!cfg.curseforge_api_key.has_value(), "default Config has no API key");

    cfg.wow_path = (workDir / "WoW" / "_retail_").string();
    auto dir = cfg.addonsDir();
    check(dir == fs::path(*cfg.wow_path) / "Interface" / "AddOns",
          "addonsDir() resolves correctly with no API key set");
}

void testTocReader(const fs::path& workDir) {
    auto folder = workDir / "AddOns" / "DBM-Core";
    fs::create_directories(folder);

    std::ofstream toc(folder / "DBM-Core.toc");
    toc << "## Interface: 110002\n"
           "## Title: Deadly Boss Mods\n"
           "## Version: 10.2.5\n"
           "## X-Curse-Project-ID: 3358\n"
           "## X-WoWI-ID: 6366\n";
    toc.close();

    auto meta = TocReader::readFolder(folder);
    check(meta.curseProjectId.has_value() && *meta.curseProjectId == 3358,
          "TocReader parses X-Curse-Project-ID");
    check(meta.wowiId.has_value() && *meta.wowiId == "6366", "TocReader parses X-WoWI-ID");
    check(meta.title.has_value() && *meta.title == "Deadly Boss Mods", "TocReader parses Title");
    check(meta.version.has_value() && *meta.version == "10.2.5", "TocReader parses Version");

    auto untagged = workDir / "AddOns" / "NoTagAddon";
    fs::create_directories(untagged);
    std::ofstream toc2(untagged / "NoTagAddon.toc");
    toc2 << "## Interface: 110002\n## Title: No Tag Addon\n";
    toc2.close();

    auto meta2 = TocReader::readFolder(untagged);
    check(!meta2.curseProjectId.has_value(), "TocReader leaves curseProjectId unset when absent");
    check(meta2.title.has_value() && *meta2.title == "No Tag Addon", "TocReader still reads Title without the tag");

    auto empty = workDir / "AddOns" / "JunkNoToc";
    fs::create_directories(empty);
    std::ofstream(empty / "readme.txt") << "not a toc\n";
    auto meta3 = TocReader::readFolder(empty);
    check(meta3.empty(), "TocReader returns empty metadata for a folder with no .toc file");
}

void testReconciler(const fs::path& workDir) {
    auto addonsDir = workDir / "ReconcileAddOns";
    fs::remove_all(addonsDir);

    auto write = [&](const std::string& folder, const std::string& tocBody) {
        auto dir = addonsDir / folder;
        fs::create_directories(dir);
        std::ofstream(dir / (folder + ".toc")) << tocBody;
    };

    write("DBM-Core", "## X-Curse-Project-ID: 3358\n");
    write("DBM-StatusBarTimers", "## X-Curse-Project-ID: 3358\n");
    write("UntaggedAddon", "## Title: No tag here\n");
    write("AlreadyTracked", "## X-Curse-Project-ID: 99999\n");

    StateStore state; // empty in-memory store (not loaded from disk)
    InstalledAddon tracked;
    tracked.modId = 99999;
    tracked.folders = {"AlreadyTracked"};
    state.upsert(tracked);

    auto entries = Reconciler::scan(addonsDir, state);
    check(entries.size() == 3, "scan skips folders already covered by state");

    auto groups = Reconciler::groupByModId(entries);
    check(groups.size() == 2, "groupByModId groups DBM's two folders under one mod id");

    auto dbmGroup = std::find_if(groups.begin(), groups.end(),
        [](const auto& g) { return g.first == 3358; });
    check(dbmGroup != groups.end() && dbmGroup->second.size() == 2,
          "the mod-3358 group contains both DBM folders");

    auto untaggedGroup = std::find_if(groups.begin(), groups.end(),
        [](const auto& g) { return g.first == 0; });
    check(untaggedGroup != groups.end() && untaggedGroup->second.size() == 1,
          "untagged folders are grouped under key 0");
}

} // namespace

int main() {
    auto workDir = fs::temp_directory_path() / "wam_test_run";
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    testExtractZip(workDir);
    testZipSlipRejected(workDir);
    testStateStoreRoundtrip(workDir);
    testSelectBestFile();
    testNoApiKeyStillUsable(workDir);
    testTocReader(workDir);
    testReconciler(workDir);

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    fs::remove_all(workDir);
    return g_failures == 0 ? 0 : 1;
}
