// Lightweight hand-rolled test harness: no test framework dependency,
// matching the project's lightweight-tooling preference. Exercises the
// parts that don't require live network/API access: zip extraction and the
// transactional install/rollback path, state persistence, .toc reading,
// reconciliation, and file/flavor selection logic.

#include "core/addon_installer.hpp"
#include "core/state_store.hpp"
#include "core/curseforge_client.hpp"
#include "core/config.hpp"
#include "core/toc_reader.hpp"
#include "core/reconciler.hpp"
#include "core/flavor_cache.hpp"
#include "core/html_text.hpp"

#include <zip.h>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sstream>

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

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

// zip_source_buffer with freep=0 does NOT copy the data: libzip reads it
// lazily, only when zip_close() actually writes the archive. `contents`
// therefore has to outlive zip_close(), which it does (same scope).
void buildZip(const fs::path& destPath,
              const std::vector<std::pair<std::string, std::string>>& entries) {
    int errCode = 0;
    zip_t* z = zip_open(destPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errCode);
    if (!z) throw std::runtime_error("failed to create test zip");

    std::vector<std::string> contents;
    contents.reserve(entries.size());
    for (const auto& e : entries) contents.push_back(e.second);

    for (size_t i = 0; i < entries.size(); ++i) {
        zip_source_t* src = zip_source_buffer(z, contents[i].data(), contents[i].size(), 0);
        zip_file_add(z, entries[i].first.c_str(), src, ZIP_FL_ENC_UTF_8);
    }
    zip_close(z);
}

bool noWamLeftovers(const fs::path& addonsDir) {
    return !fs::exists(addonsDir / ".wam-staging") &&
           !fs::exists(addonsDir / ".wam-backup") &&
           !fs::exists(addonsDir / ".wam-trash");
}

// Builds a small synthetic addon zip at destPath containing:
//   TestAddon/TestAddon.toc
//   TestAddon/TestAddon.lua
//   TestAddon/Libs/Lib.lua
void buildSyntheticAddonZip(const fs::path& destPath) {
    buildZip(destPath, {
        {"TestAddon/TestAddon.toc", "## Interface: 110000\n## Title: TestAddon\n"},
        {"TestAddon/TestAddon.lua", "print('hello from TestAddon')\n"},
        {"TestAddon/Libs/Lib.lua", "-- a nested library file\n"},
    });
}

void buildZipSlipZip(const fs::path& destPath) {
    buildZip(destPath, {{"../../evil.lua", "malicious"}});
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

    auto absZip = workDir / "abs.zip";
    auto absTarget = workDir / "abs_escape_target.lua";
    buildZip(absZip, {{absTarget.string(), "malicious"}}); // entry name is an absolute path
    threw = false;
    try {
        AddonInstaller::extractZip(absZip, workDir / "AddOns2b");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "extractZip rejects absolute-path zip entries");
    check(!fs::exists(absTarget), "an absolute-path entry never writes outside the destination");
}

void testInstallZipSwap(const fs::path& workDir) {
    auto addonsDir = workDir / "SwapAddOns";

    // v1 ships Foo + FooOptions; v2 drops FooOptions and adds a file to Foo.
    auto v1 = workDir / "foo_v1.zip";
    buildZip(v1, {
        {"Foo/Foo.toc", "## Title: Foo\n"},
        {"Foo/core.lua", "v1\n"},
        {"FooOptions/FooOptions.toc", "## Title: Foo Options\n"},
    });
    auto v2 = workDir / "foo_v2.zip";
    buildZip(v2, {
        {"Foo/Foo.toc", "## Title: Foo\n"},
        {"Foo/core.lua", "v2\n"},
        {"Foo/extra.lua", "new file\n"},
    });

    auto f1 = AddonInstaller::installZip(v1, addonsDir);
    check(f1.size() == 2 && fs::exists(addonsDir / "FooOptions"), "installZip fresh install lays down every module");
    check(noWamLeftovers(addonsDir), "installZip leaves no staging/backup/trash after a fresh install");

    auto f2 = AddonInstaller::installZip(v2, addonsDir, f1);
    check(f2.size() == 1 && f2[0] == "Foo", "installZip returns the new folder set");
    check(readAll(addonsDir / "Foo" / "core.lua") == "v2\n", "installZip replaces the old version's files");
    check(fs::exists(addonsDir / "Foo" / "extra.lua"), "installZip installs files new in this version");
    check(!fs::exists(addonsDir / "FooOptions"), "installZip removes a module the new version no longer ships");
    check(noWamLeftovers(addonsDir), "installZip leaves no staging/backup/trash after an update");
}

void testInstallZipBadZipLeavesAddonsUntouched(const fs::path& workDir) {
    auto addonsDir = workDir / "BadZipAddOns";
    writeFile(addonsDir / "Foo" / "core.lua", "original\n");

    // A valid folder followed by a zip-slip entry: extraction fails partway,
    // which used to happen *after* the old folders had already been deleted.
    auto bad = workDir / "bad.zip";
    buildZip(bad, {
        {"Foo/core.lua", "should never land\n"},
        {"../../evil.lua", "malicious"},
    });

    bool threw = false;
    try { AddonInstaller::installZip(bad, addonsDir, {"Foo"}); }
    catch (const std::exception&) { threw = true; }

    check(threw, "installZip fails on a bad zip");
    check(readAll(addonsDir / "Foo" / "core.lua") == "original\n",
          "a failed extraction leaves the installed addon exactly as it was");
    check(noWamLeftovers(addonsDir), "a failed extraction cleans up its staging folder");

    // Not a zip at all.
    auto garbage = workDir / "garbage.zip";
    writeFile(garbage, "this is not a zip file");
    threw = false;
    try { AddonInstaller::installZip(garbage, addonsDir, {"Foo"}); }
    catch (const std::exception&) { threw = true; }
    check(threw && readAll(addonsDir / "Foo" / "core.lua") == "original\n",
          "a corrupt archive fails without touching the installed addon");
}

void testInstallZipRejectsEmptyAndUnsafe(const fs::path& workDir) {
    auto addonsDir = workDir / "EmptyZipAddOns";
    writeFile(addonsDir / "Foo" / "core.lua", "original\n");

    auto rootOnly = workDir / "root_only.zip";
    buildZip(rootOnly, {{"readme.txt", "no addon folder in here\n"}});
    bool threw = false;
    try { AddonInstaller::installZip(rootOnly, addonsDir, {"Foo"}); }
    catch (const std::exception&) { threw = true; }
    check(threw && readAll(addonsDir / "Foo" / "core.lua") == "original\n",
          "a zip with no addon folders is refused and the old version is kept");

    threw = false;
    try { AddonInstaller::installZip(rootOnly, addonsDir, {"../Foo"}); }
    catch (const std::exception&) { threw = true; }
    check(threw, "installZip refuses replaceFolders entries that are not a plain folder name");

    auto wamDir = workDir / "wamdir.zip";
    buildZip(wamDir, {{".wam-backup/Foo/core.lua", "x\n"}});
    threw = false;
    try { AddonInstaller::installZip(wamDir, addonsDir, {}); }
    catch (const std::exception&) { threw = true; }
    check(threw, "a zip cannot install into wam's own .wam-* working folders");
}

void testInstallZipRollbackAfterPartialSwap(const fs::path& workDir) {
    auto addonsDir = workDir / "RollbackAddOns";
    writeFile(addonsDir / "Old" / "old.lua", "old addon file\n");
    writeFile(addonsDir / "A" / "a.lua", "A original\n"); // will collide with the new zip's A

    auto zip = workDir / "two_folders.zip";
    buildZip(zip, {
        {"A/a.lua", "A new\n"},
        {"B/b.lua", "B new\n"},
    });

    // Fail after A has been renamed into place but before B: the worst case,
    // where the swap is genuinely half-done.
    AddonInstaller::failAfterInstalledForTesting = 1;
    bool threw = false;
    try { AddonInstaller::installZip(zip, addonsDir, {"Old"}); }
    catch (const std::exception&) { threw = true; }
    AddonInstaller::failAfterInstalledForTesting = -1;

    check(threw, "installZip reports a failure that happens mid-swap");
    check(readAll(addonsDir / "Old" / "old.lua") == "old addon file\n", "rollback restores the replaced addon's folder");
    check(readAll(addonsDir / "A" / "a.lua") == "A original\n", "rollback restores a pre-existing folder that was overwritten");
    check(!fs::exists(addonsDir / "B"), "rollback removes folders that were never meant to survive");
    check(noWamLeftovers(addonsDir), "rollback leaves no staging/backup/trash behind");
}

void testRecoverInterrupted(const fs::path& workDir) {
    auto addonsDir = workDir / "RecoverAddOns";
    fs::create_directories(addonsDir);

    // Simulate a process killed mid-swap: Foo was moved to backup and a
    // half-installed replacement was left in its place, plus stray staging.
    writeFile(addonsDir / ".wam-backup" / "Foo" / "core.lua", "pre-swap version\n");
    writeFile(addonsDir / "Foo" / "core.lua", "half-installed\n");
    writeFile(addonsDir / ".wam-staging" / "Bar" / "bar.lua", "staged\n");

    auto restored = AddonInstaller::recoverInterrupted(addonsDir);
    check(restored == 1, "recoverInterrupted reports the folder it restored");
    check(readAll(addonsDir / "Foo" / "core.lua") == "pre-swap version\n",
          "recoverInterrupted puts the pre-swap folder back over the partial one");
    check(noWamLeftovers(addonsDir), "recoverInterrupted clears backup and staging");
    check(AddonInstaller::recoverInterrupted(addonsDir) == 0, "recoverInterrupted is a no-op on a clean directory");
}

void testRemoveFoldersRejectsTraversal(const fs::path& workDir) {
    auto addonsDir = workDir / "RemoveAddOns";
    fs::create_directories(addonsDir / "Legit");
    auto victim = workDir / "victim";
    writeFile(victim / "precious.txt", "do not delete\n");

    AddonInstaller::removeFolders(addonsDir, {"../victim", "Legit"});
    check(fs::exists(victim / "precious.txt"), "removeFolders ignores a folder name that escapes addonsDir");
    check(!fs::exists(addonsDir / "Legit"), "removeFolders still removes ordinary folder names");
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
    a.flavorTypeId = 517;
    a.flavorName = "Retail";
    a.iconUrl = "https://media.forgecdn.net/a/thumb.png";
    a.author = "Ellesmere";

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
        check(found->flavorTypeId == 517 && found->flavorName == "Retail", "persisted flavor round-trips");
        check(found->iconUrl == "https://media.forgecdn.net/a/thumb.png", "persisted iconUrl round-trips");
        check(found->author == "Ellesmere", "persisted author round-trips");
    }

    bool removed = reloaded.remove(12345);
    reloaded.save();
    auto afterRemove = StateStore::load();
    check(removed, "remove() reports it removed an entry");
    check(!afterRemove.find(12345).has_value(), "removed addon no longer present after reload");
}

void testStateStoreLegacyFile(const fs::path& workDir) {
    // installed.json written before flavors existed must still load.
    auto dataDir = workDir / "legacy_data";
    setenv("XDG_DATA_HOME", dataDir.string().c_str(), 1);
    writeFile(dataDir / "wow-addon-manager" / "installed.json",
              R"({"addons":[{"modId":7,"fileId":8,"displayName":"Old","fileName":"old.zip","channel":1,)"
              R"("gameVersions":[],"folders":["Old"],"installedAt":"2026-01-01T00:00:00Z","manuallyProvided":false}]})");
    auto store = StateStore::load();
    auto found = store.find(7);
    check(found.has_value() && found->flavorTypeId == 0 && found->flavorName.empty() && found->iconUrl.empty() &&
          found->fileDisplayName.empty() && found->fileDate.empty() && found->modSlug.empty() && found->author.empty() && !found->adopted,
          "a state file with no flavor/icon/detail/author fields loads as unknown");
    setenv("XDG_DATA_HOME", workDir.string().c_str(), 1);
}

void testParseModResponses() {
    // Shape follows the CurseForge Mod object: logo is a ModAsset, and
    // latestFilesIndexes carry a per-file gameVersionTypeId (the flavor).
    const std::string list = R"({"data":[
      {"id":1,"name":"Alpha","slug":"alpha","summary":"s",
       "authors":[{"id":1,"name":"Ellesmere","url":"u"},{"id":2,"name":"Helper","url":"u2"},{"id":3,"url":"nameless"}],
       "downloadCount":1234567.0,
       "latestFiles":[{"id":40,"displayName":"v1.0","fileName":"a-1.zip","fileDate":"2026-01-01T00:00:00Z"},
                      {"id":90,"displayName":"v2.0","fileName":"a-2.zip","fileDate":"2026-09-29T14:03:11.5Z"},
                      {"id":60,"displayName":"v1.5","fileName":"a-15.zip","fileDate":"2026-05-05T00:00:00Z"}],
       "logo":{"id":9,"modId":1,"title":"t","thumbnailUrl":"https://media.forgecdn.net/a/thumb.png","url":"https://media.forgecdn.net/a/full.png"},
       "latestFilesIndexes":[{"gameVersion":"11.0.7","fileId":5,"gameVersionTypeId":517},
                             {"gameVersion":"11.0.5","fileId":4,"gameVersionTypeId":517},
                             {"gameVersion":"1.15.5","fileId":3,"gameVersionTypeId":67408}]},
      {"id":2,"name":"Beta","slug":"beta","summary":"","dateReleased":"2025-03-04T05:06:07Z",
       "latestFiles":[{"id":5,"fileName":"b-5.zip"}],
       "logo":{"url":"https://media.forgecdn.net/b/full.png","thumbnailUrl":null}},
      {"id":3,"name":"Gamma","slug":"gamma","summary":"","logo":null}
    ]})";
    auto mods = CurseForgeClient::parseModList(list);
    check(mods.size() == 3, "parseModList reads every mod");
    check(mods[0].logoUrl == "https://media.forgecdn.net/a/thumb.png", "logoUrl prefers the thumbnail");
    check(mods[0].gameVersionTypeIds == std::vector<int64_t>({517, 67408}),
          "gameVersionTypeIds are collected from latestFilesIndexes without duplicates");
    check(mods[0].author == "Ellesmere, Helper", "author joins every named author and skips one with no name");
    check(mods[1].author.empty() && mods[2].author.empty(), "a mod with no authors array has an empty author");
    check(mods[1].logoUrl == "https://media.forgecdn.net/b/full.png", "logoUrl falls back to the full logo url");
    check(mods[0].downloadCount == 1234567, "downloadCount is read");
    check(mods[0].latestVersion == "v2.0" && mods[0].latestDate == "2026-09-29T14:03:11.5Z",
          "the latest version and date come from the newest of latestFiles (highest id)");
    check(mods[1].latestVersion == "b-5.zip" && mods[1].latestDate == "2025-03-04T05:06:07Z" && mods[1].downloadCount == 0,
          "a file with no display name falls back to its file name, a missing file date to dateReleased, a missing count to 0");
    check(mods[2].latestVersion.empty() && mods[2].latestDate.empty(), "a mod with no latestFiles has no latest version or date");
    check(mods[2].logoUrl.empty() && mods[2].gameVersionTypeIds.empty(),
          "a mod with a null logo and no file indexes parses with empty icon/flavors");

    auto one = CurseForgeClient::parseModObject(R"({"data":{"id":7,"name":"Solo","logo":{"thumbnailUrl":"u"}}})");
    check(one.id == 7 && one.logoUrl == "u", "parseModObject reads a single mod");
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

void testMatchFlavor() {
    // Deliberately lists the more specific names first, so a naive
    // first-substring-match would resolve "classic" to the wrong flavor.
    std::vector<GameVersionType> types = {
        {1, "Burning Crusade Classic", "burning-crusade-classic"},
        {2, "Classic Era", "classic-era"},
        {3, "Classic", "classic"},
        {4, "Retail", "retail"},
    };
    auto classic = CurseForgeClient::matchFlavor(types, "classic");
    check(classic.has_value() && *classic == 3, "matchFlavor prefers an exact name/slug over a substring hit");
    auto retail = CurseForgeClient::matchFlavor(types, "RETAIL");
    check(retail.has_value() && *retail == 4, "matchFlavor is case-insensitive");
    auto era = CurseForgeClient::matchFlavor(types, "classic era");
    check(era.has_value() && *era == 2, "matchFlavor matches a multi-word flavor name");
    auto sub = CurseForgeClient::matchFlavor(types, "crusade");
    check(sub.has_value() && *sub == 1, "matchFlavor falls back to a substring match");
    check(!CurseForgeClient::matchFlavor(types, "nonsense").has_value(), "matchFlavor returns nullopt for no match");
}

void testFlavorForFolder() {
    std::vector<GameVersionType> types = {
        {10, "Retail", "retail"},
        {20, "Classic Era", "classic-era"},
        {30, "Burning Crusade Classic", "burning-crusade-classic"},
        {40, "Forever", "forever"},
    };
    auto id = [&](const char* folder) { return CurseForgeClient::matchFlavorForFolder(types, folder); };
    check(id("_retail_") == std::optional<int64_t>(10), "a _retail_ folder resolves to Retail");
    check(id("_classic_era_") == std::optional<int64_t>(20), "a _classic_era_ folder resolves to Classic Era (name match)");
    check(id("_forever_") == std::optional<int64_t>(40), "a folder named after a newly added flavor resolves without any hardcoding");
    check(id("_ptr_") == std::optional<int64_t>(10) && id("_xptr_") == std::optional<int64_t>(10) && id("_beta_") == std::optional<int64_t>(10),
          "retail test realms share Retail's addons");
    check(id("_classic_era_ptr_") == std::optional<int64_t>(20), "a classic era test realm resolves to Classic Era");
    check(!id("_classic_").has_value(), "an ambiguous folder such as _classic_ is never guessed");
    check(!id("World of Warcraft").has_value() && !id("").has_value() && !id("_ptr_x_").has_value(),
          "unknown and empty folder names resolve to nothing");
}

void testConfigFlavorRoundtrip(const fs::path& workDir) {
    setenv("XDG_CONFIG_HOME", (workDir / "cfg_flavor").string().c_str(), 1);
    Config cfg;
    cfg.wow_path = "/games/World of Warcraft/_retail_/";
    check(cfg.wowFolderName() == "_retail_", "wowFolderName ignores a trailing slash");
    cfg.wow_path = "/games/wow/_classic_era_";
    check(cfg.wowFolderName() == "_classic_era_", "wowFolderName returns the last path component");
    check(Config().wowFolderName().empty(), "wowFolderName is empty when no path is set");

    cfg.wow_flavor_id = 517;
    cfg.wow_flavor_name = "Retail";
    cfg.save();
    auto back = Config::load();
    check(back.wow_flavor_id == std::optional<int64_t>(517) && back.wow_flavor_name == std::optional<std::string>("Retail"),
          "the WoW folder's flavor round-trips through config.json");

    Config none; none.wow_path = "/x";
    none.save();
    auto backNone = Config::load();
    check(!backNone.wow_flavor_id.has_value() && !backNone.wow_flavor_name.has_value(),
          "a config with no flavor loads as unset (also covers files written before this field existed)");
}

void testParseFilesPage() {
    const std::string body = R"({"data":[
      {"id":30,"modId":1,"displayName":"v3","fileName":"a-3.zip","releaseType":1,"fileDate":"2026-09-29T14:03:11.5Z","downloadUrl":"u"},
      {"id":20,"modId":1,"displayName":"v2","fileName":"a-2.zip","releaseType":2,"fileDate":null,"downloadUrl":null}
    ],"pagination":{"index":0,"pageSize":2,"resultCount":2,"totalCount":5}})";
    auto page = CurseForgeClient::parseFilesPage(body);
    check(page.files.size() == 2 && page.totalCount == 5 && page.index == 0, "parseFilesPage reads the files and the pagination totals");
    check(page.hasMore(), "hasMore is true while files remain beyond this page");
    check(page.files[0].fileDate == "2026-09-29T14:03:11.5Z" && page.files[1].fileDate.empty(),
          "fileDate is read, and a null date becomes empty");
    check(page.files[1].isBlocked() && !page.files[0].isBlocked(), "blocked downloads still parse alongside dates");

    auto noPagination = CurseForgeClient::parseFilesPage(R"({"data":[{"id":1,"modId":1}]})");
    check(noPagination.totalCount == 1 && !noPagination.hasMore(), "a response without pagination counts as one complete page");
    auto last = CurseForgeClient::parseFilesPage(
        R"({"data":[{"id":1,"modId":1}],"pagination":{"index":4,"pageSize":2,"resultCount":1,"totalCount":5}})");
    check(last.index == 4 && !last.hasMore(), "the final page reports no more");
}

void testFlavorCache(const fs::path& workDir) {
    setenv("XDG_CONFIG_HOME", (workDir / "cfg_cache").string().c_str(), 1);
    check(FlavorCache::load().all().empty(), "a missing flavors.json loads as an empty cache");

    FlavorCache c;
    c.merge({{10, "Retail", "wow-retail"}, {40, "Forever", "wow-forever"}});
    check(c.all().size() == 2 && c.all()[1].slug == "wow-forever" && c.all()[1].name == "Forever" && c.all()[1].apiName == "Forever",
          "merge adds new flavors keyed by CurseForge's slug, in the API's order");
    check(c.nameFor(40) == "Forever" && c.nameFor(999, "?") == "?", "nameFor looks a display name up by id, with a fallback");

    check(c.rename("wow-forever", "WoW Forever") && c.nameFor(40) == "WoW Forever", "rename changes the display name");
    check(!c.rename("nope", "x"), "renaming an unknown key reports failure");

    // CurseForge renames things and reissues ids: the user's edit survives, the rest follows.
    c.merge({{10, "Retail (Midnight)", "wow-retail"}, {41, "Forever", "wow-forever"}});
    check(c.findById(10)->name == "Retail (Midnight)" && c.findById(10)->apiName == "Retail (Midnight)",
          "an unedited name follows CurseForge's rename");
    check(c.nameFor(41) == "WoW Forever" && !c.findById(40).has_value() && c.findById(41)->apiName == "Forever",
          "an edited name is kept while the id and CurseForge's name are refreshed");

    check(c.rename("wow-forever", "") && c.nameFor(41) == "Forever", "an empty name resets to CurseForge's");

    c.rename("wow-forever", "Ever");
    c.merge({{10, "Retail (Midnight)", "wow-retail"}}); // Forever vanished upstream
    check(c.findById(41).has_value() && c.all().size() == 2, "a flavor CurseForge stops listing is kept");

    c.save();
    auto back = FlavorCache::load();
    check(back.all().size() == 2 && back.all()[0].slug == "wow-retail" && back.nameFor(41) == "Ever" && back.findById(41)->apiName == "Forever",
          "the cache round-trips through flavors.json, order and edits included");

    auto types = back.asTypes();
    check(types.size() == 2 && types[1].name == "Ever" && types[1].slug == "wow-forever" && types[1].id == 41,
          "asTypes exposes display names for matching");
    check(back.asTypes(true)[1].name == "Forever", "asTypes(true) exposes CurseForge's own names instead");

    // Hand-edited short form and a damaged file.
    writeFile(FlavorCache::path(), R"({"wow-retail": "Live", "wow-forever": {"id": 41, "name": "Ever", "api_name": "Forever"}})");
    auto hand = FlavorCache::load();
    check(hand.all().size() == 2 && hand.all()[0].name == "Live" && hand.all()[0].id == 0,
          "a hand-written \"key\": \"name\" entry loads with the name and no id yet");
    hand.merge({{10, "Retail", "wow-retail"}});
    check(hand.findById(10).has_value() && hand.nameFor(10) == "Live" && hand.findById(10)->apiName == "Retail",
          "merge fills in the id of a hand-written entry and keeps the name you typed");

    writeFile(FlavorCache::path(), "{ not json");
    check(FlavorCache::load().all().empty(), "a damaged flavors.json loads as empty instead of throwing");

    GameVersionType noSlug{7, "Some New Flavor", ""};
    check(FlavorCache::keyFor(noSlug) == "some-new-flavor", "a flavor with no slug gets a key made from its name");
}

void testHtmlToPlainText() {
    check(htmlToPlainText("<p>Major Features:</p><ul><li>Action bars &amp; more</li><li>Flight timer</li></ul><br>Done")
              == "Major Features:\n\n- Action bars & more\n- Flight timer\n\nDone",
          "htmlToPlainText turns paragraphs, list items and breaks into lines and bullets");
    check(htmlToPlainText("**Major Features:**\n- **Action Bars: End Caps**") == "**Major Features:**\n- **Action Bars: End Caps**",
          "text that already contains markdown is left as it is");
    check(htmlToPlainText("a &lt;b&gt; &quot;q&quot; &#39;s&#39; &#x2713; &nbsp;x &unknown; &") == "a <b> \"q\" 's' \xE2\x9C\x93  x &unknown; &",
          "entities are decoded, unknown ones and a bare ampersand stay literal");
    check(htmlToPlainText("x<!-- hidden -->y <a href=\"u\">link</a> 1 < 2") == "xy link 1 < 2",
          "comments and other tags vanish, a stray < survives");
    check(htmlToPlainText("<p>a</p><p></p><p></p><p>b</p>") == "a\n\nb", "runs of blank lines collapse to one");
    check(htmlToPlainText("  \n<p> </p>").empty() && htmlToPlainText("").empty(), "empty and whitespace-only input give an empty string");
}

void testFileDetailsAndLookups(const fs::path& workDir) {
    auto list = CurseForgeClient::parseFileList(
        R"({"data":[{"id":5,"modId":1,"displayName":"v5","fileName":"a-5.zip","releaseType":2,"fileDate":"2026-09-29T14:03:11.5Z"},
                    {"id":6,"modId":2,"fileName":"b-6.zip","fileDate":null}]})");
    check(list.size() == 2 && list[0].displayName == "v5" && list[0].fileDate == "2026-09-29T14:03:11.5Z" && list[1].modId == 2,
          "parseFileList reads a batch of files from different mods");
    check(CurseForgeClient::parseChangelog(R"({"data":"<p>hi</p>"})") == "<p>hi</p>" && CurseForgeClient::parseChangelog(R"({"data":null})").empty(),
          "parseChangelog returns the HTML, or nothing for a null changelog");
    check(CurseForgeClient::browserDownloadUrl(26886, 8875044) == "https://www.curseforge.com/api/v1/mods/26886/files/8875044/download",
          "browserDownloadUrl builds the website's direct download link");
    check(CurseForgeClient::modPageUrl("questie") == "https://www.curseforge.com/wow/addons/questie", "modPageUrl builds the addon page link");

    InstalledAddon a;
    a.modId = 1; a.folders = {"Keep"}; a.flavorTypeId = 9; a.installedAt = "then";
    recordFile(a, list[0]);
    check(a.fileId == 5 && a.fileName == "a-5.zip" && a.fileDisplayName == "v5" && a.fileDate == "2026-09-29T14:03:11.5Z" &&
          a.channel == ReleaseChannel::Beta,
          "recordFile stores the file's id, names, date and channel");
    check(a.folders == std::vector<std::string>({"Keep"}) && a.flavorTypeId == 9 && a.installedAt == "then",
          "recordFile leaves folders, flavor and install time alone");

    setenv("XDG_DATA_HOME", (workDir / "details_data").string().c_str(), 1);
    a.modSlug = "my-addon"; a.adopted = true;
    auto store = StateStore::load();
    store.upsert(a);
    store.save();
    auto back = StateStore::load().find(1);
    check(back && back->fileDisplayName == "v5" && back->fileDate == "2026-09-29T14:03:11.5Z" && back->modSlug == "my-addon" && back->adopted,
          "file details, slug and the adopted flag round-trip through installed.json");
    // legacy files (no such fields) are covered by testStateStoreLegacyFile
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
    write(".wam-staging", "## X-Curse-Project-ID: 1\n"); // wam's own working dir, must never be reported

    StateStore state; // empty in-memory store (not loaded from disk)
    InstalledAddon tracked;
    tracked.modId = 99999;
    tracked.folders = {"AlreadyTracked"};
    state.upsert(tracked);

    auto entries = Reconciler::scan(addonsDir, state);
    check(entries.size() == 3, "scan skips folders already covered by state and wam's own .wam-* folders");

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

void testReconcilerAdopt(const fs::path& workDir) {
    auto addonsDir = workDir / "AdoptAddOns";
    fs::remove_all(addonsDir);
    fs::create_directories(addonsDir / "Foo");
    fs::create_directories(addonsDir / "FooOptions");
    fs::create_directories(addonsDir / "Owned");

    StateStore state;
    InstalledAddon other;
    other.modId = 99; other.displayName = "Other"; other.folders = {"Owned"};
    state.upsert(other);

    auto rec = Reconciler::adopt(state, addonsDir, 5, "Foo Mod", "http://icon", {"Foo"});
    check(rec.modId == 5 && rec.fileId == 0 && rec.displayName == "Foo Mod" && rec.iconUrl == "http://icon",
          "adopt creates a new entry with fileId 0 (version unknown) and the icon");
    check(state.find(5).has_value() && state.find(5)->folders == std::vector<std::string>({"Foo"}),
          "adopt records the entry in state");
    check(rec.adopted && rec.fileDisplayName.empty() && rec.modSlug.empty(), "an adopted entry is flagged and has no file details yet");

    auto merged = Reconciler::adopt(state, addonsDir, 5, "ignored", "http://other", {"Foo", "FooOptions"});
    check(merged.folders == std::vector<std::string>({"Foo", "FooOptions"}),
          "adopting more folders merges into the existing entry without duplicates");
    check(merged.displayName == "Foo Mod" && merged.iconUrl == "http://icon",
          "merging keeps the existing name and icon");

    auto expectThrow = [&](const std::string& what, auto fn) {
        bool threw = false;
        try { fn(); } catch (const std::exception&) { threw = true; }
        check(threw, what);
    };
    auto before = state.all().size();
    expectThrow("adopt refuses a folder owned by a different mod",
                [&] { Reconciler::adopt(state, addonsDir, 7, "X", "", {"Owned"}); });
    expectThrow("adopt refuses a folder that does not exist",
                [&] { Reconciler::adopt(state, addonsDir, 7, "X", "", {"Missing"}); });
    expectThrow("adopt refuses a name that escapes AddOns",
                [&] { Reconciler::adopt(state, addonsDir, 7, "X", "", {"../evil"}); });
    expectThrow("adopt refuses wam's own working folders",
                [&] { Reconciler::adopt(state, addonsDir, 7, "X", "", {".wam-backup"}); });
    expectThrow("adopt refuses an invalid mod id",
                [&] { Reconciler::adopt(state, addonsDir, 0, "X", "", {"Foo"}); });

    fs::create_directories(addonsDir / "Fresh");
    expectThrow("adopt is all-or-nothing when one folder in the batch is bad",
                [&] { Reconciler::adopt(state, addonsDir, 8, "Y", "", {"Fresh", "Missing"}); });
    check(!state.find(8).has_value() && state.all().size() == before,
          "a rejected adopt leaves state completely untouched");
}

} // namespace

int main() {
    auto workDir = fs::temp_directory_path() / "wam_test_run";
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    testExtractZip(workDir);
    testZipSlipRejected(workDir);
    testInstallZipSwap(workDir);
    testInstallZipBadZipLeavesAddonsUntouched(workDir);
    testInstallZipRejectsEmptyAndUnsafe(workDir);
    testInstallZipRollbackAfterPartialSwap(workDir);
    testRecoverInterrupted(workDir);
    testRemoveFoldersRejectsTraversal(workDir);
    testStateStoreRoundtrip(workDir);
    testStateStoreLegacyFile(workDir);
    testSelectBestFile();
    testMatchFlavor();
    testParseModResponses();
    testFlavorForFolder();
    testConfigFlavorRoundtrip(workDir);
    testParseFilesPage();
    testFlavorCache(workDir);
    testHtmlToPlainText();
    testFileDetailsAndLookups(workDir);
    testNoApiKeyStillUsable(workDir);
    testTocReader(workDir);
    testReconciler(workDir);
    testReconcilerAdopt(workDir);

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    fs::remove_all(workDir);
    return g_failures == 0 ? 0 : 1;
}
