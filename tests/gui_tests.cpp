// Hand-rolled checks for the Qt adapter layer (same style as test_main.cpp):
// the list models, the update review queue, and one end-to-end pass through
// WamController's worker thread with no API key and no network.

#include "gui/wam_controller.hpp"
#include "gui/pending_updates_model.hpp"
#include "gui/search_results_model.hpp"
#include "gui/installed_addons_model.hpp"
#include "gui/scan_results_model.hpp"
#include "gui/toc_text.hpp"
#include "gui/install_files_model.hpp"
#include "gui/flavors_model.hpp"
#include "gui/format.hpp"
#include "core/flavor_cache.hpp"
#include "core/http_client.hpp"
#include "core/state_store.hpp"
#include "core/profiles.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <zip.h>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <map>
#include <mutex>
#include <vector>

using namespace wam;
using namespace wam::gui;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
    else std::cout << "ok:   " << what << "\n";
}

PendingUpdate makeUpdate(qint64 id, bool blocked = false) {
    PendingUpdate u;
    u.modId = id;
    u.modName = QString("Mod %1").arg(id);
    u.latestFileId = id * 10;
    u.latestFileName = "latest.zip";
    u.blocked = blocked;
    return u;
}

void testPendingUpdatesModel() {
    PendingUpdatesModel m;

    // The review popup binds to head's fields even while the queue is empty
    // (that is what produced "Unable to assign [undefined] to QString" in
    // QML), so every key must exist and be a string/bool, never missing.
    {
        const auto h = m.head();
        const QStringList strKeys = {"modName", "modSlug", "flavorName", "currentFile", "latestFile", "iconUrl", "status", "error"};
        bool allPresent = m.count() == 0;
        for (const auto& k : strKeys) allPresent = allPresent && h.contains(k) && h.value(k).typeId() == QMetaType::QString;
        allPresent = allPresent && h.contains("modId") && h.contains("latestFileId") &&
                     h.value("blocked").typeId() == QMetaType::Bool;
        check(allPresent, "an empty queue's head is a full map of defaults, never missing keys");
    }

    m.add(makeUpdate(1));
    m.add(makeUpdate(2, /*blocked=*/true));
    m.add(makeUpdate(3));
    check(m.count() == 3, "add() appends rows");
    check(m.head().value("modId").toLongLong() == 1, "head is the first row");
    check(m.applicableCount() == 2, "applicableCount skips blocked rows");

    m.add(makeUpdate(3)); // same modId again: replaced, not duplicated
    check(m.count() == 3, "add() with an existing modId replaces the row");

    m.setStatus(1, PendingUpdate::Status::Applying);
    check(m.head().value("status").toString() == "applying", "head reflects an in-flight apply");
    check(m.applicableCount() == 1, "an applying row no longer counts as applicable");

    m.setStatus(1, PendingUpdate::Status::Failed, "boom");
    check(m.head().value("status").toString() == "failed" && m.head().value("error").toString() == "boom",
          "a failed row keeps its error message");
    check(m.applicableCount() == 1, "failed rows are not re-applied by apply-all");

    m.remove(1);
    check(m.count() == 2 && m.head().value("modId").toLongLong() == 2, "removing the head advances to the next row");
    check(m.data(m.index(0), PendingUpdatesModel::BlockedRole).toBool(), "row roles expose the blocked flag");

    m.clear();
    check(m.count() == 0 && m.head().value("latestFile").toString().isEmpty() && m.head().contains("iconUrl"),
          "clear() empties the queue and the head falls back to defaults");
}

void testScanResultsModel() {
    ScanResultsModel m;
    ScanGroup tagged; tagged.modId = 3358; tagged.name = "Deadly Boss Mods";
    tagged.folders = {"DBM-Core", "DBM-GUI"}; tagged.details = {"DBM-Core v1", "DBM-GUI"};
    ScanGroup loose; loose.name = "My Hand-Made Addon"; loose.folders = {"MyAddon"}; loose.details = {"MyAddon"};
    m.setGroups({tagged, loose});

    check(m.folderCount() == 3, "folderCount totals folders across rows");
    check(m.matchedCount() == 1, "matchedCount counts only .toc-tagged rows");
    check(m.data(m.index(0), ScanResultsModel::MatchedRole).toBool() &&
          !m.data(m.index(1), ScanResultsModel::MatchedRole).toBool(),
          "matched role distinguishes tagged from untagged rows");
}

void testCleanTocText() {
    check(cleanTocText("|cff33ff99Details|r") == "Details", "cleanTocText strips colour codes");
    check(cleanTocText("  |cFFFF0000Bad|r |cff00ff00Good|r  ") == "Bad Good", "cleanTocText handles several colour runs and trims");
    check(cleanTocText("|TInterface\\Icons\\x:16|t Icon Title") == "Icon Title", "cleanTocText strips inline textures");
    check(cleanTocText("Plain Title") == "Plain Title", "cleanTocText leaves plain text alone");
}

InstallFile makeFile(qint64 id, const char* channel) {
    InstallFile f;
    f.fileId = id;
    f.displayName = QString("v%1").arg(id);
    f.channel = channel;
    return f;
}

void testInstallFilesModel() {
    InstallFilesModel m;
    check(!m.loading() && !m.loaded() && m.rowCount() == 0, "a fresh install-files model is idle and empty");

    m.begin(7, 517);
    check(m.loading() && m.matches(7, 517) && !m.matches(7, 999) && !m.matches(8, 517),
          "begin() marks the model as loading for exactly one addon and flavor");

    // Ten files, newest id first once sorted: ids 110..101; releases are every id except 103, 105, 107, 109.
    QList<InstallFile> files;
    for (qint64 id = 101; id <= 110; ++id) files.push_back(makeFile(id, (id % 2 == 1 && id > 102) ? "beta" : "release"));
    m.append(files, 0, 10);
    check(m.loaded() && !m.loading(), "append() ends the loading state");
    check(m.rowCount() == 5, "only the first five versions are shown");
    check(m.fileIdAt(0) == 110 && m.fileIdAt(1) == 108, "versions are sorted newest first and filtered to the release channel");
    check(m.filteredCount() == 6, "filteredCount counts every loaded version in the channel");
    const QVariantMap counts = m.channelCounts();
    check(counts.value("release").toInt() == 6 && counts.value("beta").toInt() == 4 && counts.value("alpha").toInt() == 0,
          "channelCounts counts every loaded version per release type, including empty ones as 0");
    check(!m.partial(), "partial is false when the server has no more pages");
    check(m.hasMore(), "hasMore is true while loaded versions are hidden");

    m.showMore();
    check(m.rowCount() == 6 && !m.hasMore(), "showMore() reveals the rest and hasMore turns false when nothing is left");

    m.setChannel("beta");
    check(m.rowCount() == 4 && m.fileIdAt(0) == 109, "switching channel shows that channel's versions");
    m.setChannel("alpha");
    check(m.rowCount() == 0 && m.loaded() && m.error().isEmpty(), "a channel with no versions is an empty list, not an error");

    // Paging: the server has more than was fetched.
    InstallFilesModel p;
    p.begin(1, 2);
    p.append({makeFile(50, "release"), makeFile(49, "beta")}, 0, 6);
    check(p.nextIndex() == 2 && p.hasMore(), "nextIndex follows the pages received and hasMore reflects the server");
    check(p.partial() && p.channelCounts().value("release").toInt() == 1 && p.channelCounts().value("beta").toInt() == 1,
          "partial is true while the server has pages not loaded, and counts cover the loaded files");
    check(p.wantsFetch(), "a short window with more on the server asks for another page");
    p.setLoading(true);
    check(!p.wantsFetch(), "no second fetch is requested while one is in flight");
    p.append({makeFile(50, "release"), makeFile(48, "release")}, 2, 6); // 50 again: must not duplicate
    check(p.nextIndex() == 4 && p.filteredCount() == 2, "a repeated file is not listed twice");
    p.append({makeFile(47, "release"), makeFile(46, "release")}, 4, 6);
    check(p.nextIndex() == 6 && !p.hasMore(), "the last page clears the server-has-more flag");

    p.fail("boom");
    check(!p.loading() && p.error() == "boom", "fail() records the error and stops loading");
    p.clear();
    check(p.rowCount() == 0 && !p.loaded() && p.error().isEmpty() && !p.matches(1, 2), "clear() forgets everything");
}

void testInstallFileFromCurseForge() {
    CurseForgeFile f;
    f.id = 9006004; f.displayName = "v9.3.2"; f.fileName = "EllesmereUI-v9.3.2.zip";
    f.releaseType = ReleaseChannel::Release;
    f.gameVersions = {"12.1.0", "12.0.7", "12.0.5"};
    f.fileDate = "2026-09-29T14:03:11.5Z";
    f.downloadUrl = "https://example/dl";
    auto r = InstallFile::fromCurseForge(f);
    check(r.displayName == "v9.3.2" && r.channel == "release" && !r.blocked, "an InstallFile carries name, channel and blocked state");
    check(r.gameVersions == "12.1.0 +2", "game versions are summarised as the first plus a count");
    check(r.date == "Sep 29, 2026", "the upload date is formatted like CurseForge's site");

    f.displayName.clear(); f.fileDate = "not a date"; f.downloadUrl.reset(); f.releaseType = ReleaseChannel::Alpha;
    auto r2 = InstallFile::fromCurseForge(f);
    check(r2.displayName == "EllesmereUI-v9.3.2.zip" && r2.date.isEmpty() && r2.blocked && r2.channel == "alpha",
          "a missing display name falls back to the file name, and a bad date to empty");
}

void testFlavorsModel() {
    FlavorsModel m;
    int counts = 0;
    QObject::connect(&m, &FlavorsModel::countChanged, [&] { ++counts; });
    m.setEntries({{10, "wow-retail", "Retail", "Retail"}, {40, "wow-forever", "Ever", "Forever"}});
    check(m.rowCount() == 2 && counts == 1, "the flavor model holds the entries and reports a new count");
    check(m.data(m.index(1), FlavorsModel::SlugRole).toString() == "wow-forever" &&
          m.data(m.index(1), FlavorsModel::NameRole).toString() == "Ever" &&
          m.data(m.index(1), FlavorsModel::FlavorIdRole).toLongLong() == 40, "slug, name and id roles");
    check(m.data(m.index(1), FlavorsModel::EditedRole).toBool() && !m.data(m.index(0), FlavorsModel::EditedRole).toBool(),
          "a name that differs from CurseForge's is marked as edited");

    int resets = 0, changed = 0;
    QObject::connect(&m, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&m, &QAbstractItemModel::dataChanged, [&] { ++changed; });
    m.setEntries({{10, "wow-retail", "Retail", "Retail"}, {40, "wow-forever", "Forever", "Forever"}});
    check(resets == 0 && changed == 1 && !m.data(m.index(1), FlavorsModel::EditedRole).toBool(),
          "renaming keeps the rows (so a text field being edited survives) and only updates their data");
    m.setEntries({{10, "wow-retail", "Retail", "Retail"}});
    check(resets == 1 && m.rowCount() == 1 && counts == 2, "a different set of flavors resets the model");
}

void testInstalledFlavorNames() {
    InstalledAddonsModel m;
    InstalledAddon a; a.modId = 1; a.flavorTypeId = 40; a.flavorName = "Old name";
    InstalledAddon b; b.modId = 2; b.flavorTypeId = 99; b.flavorName = "Stored";
    InstalledAddon c; c.modId = 3; // flavor unknown
    m.setAddons({a, b, c});
    check(m.data(m.index(0), InstalledAddonsModel::FlavorNameRole).toString() == "Old name", "without a cache the stored flavor name is shown");
    m.setFlavorNames({{40, "Forever"}});
    check(m.data(m.index(0), InstalledAddonsModel::FlavorNameRole).toString() == "Forever",
          "a name from the flavor cache replaces the one stored at install time");
    check(m.data(m.index(1), InstalledAddonsModel::FlavorNameRole).toString() == "Stored" &&
          m.data(m.index(2), InstalledAddonsModel::FlavorNameRole).toString().isEmpty(),
          "a flavor missing from the cache falls back to the stored name, and an unknown one stays empty");
    m.setAddons({a}); // a list refresh must not lose the names
    check(m.data(m.index(0), InstalledAddonsModel::FlavorNameRole).toString() == "Forever", "cached names survive a list reload");
}

void testInstalledRowTexts() {
    InstalledAddonsModel m;
    InstalledAddon adopted; adopted.modId = 1; adopted.adopted = true; adopted.installedAt = "2026-09-29T21:14:00Z";
    InstalledAddon linked;  linked.modId = 2; linked.fileId = 9006004; linked.fileDisplayName = "v9.3.2";
    linked.fileName = "EllesmereUI-v9.3.2.zip"; linked.channel = ReleaseChannel::Beta;
    linked.flavorTypeId = 10; linked.flavorName = "Retail"; linked.fileDate = "2026-09-29T14:03:11.5Z";
    linked.modSlug = "ellesmere"; linked.installedAt = "2026-09-29T21:14:00Z";
    InstalledAddon bare; bare.modId = 3; bare.fileId = 5; bare.fileName = "Bare-1.zip"; // legacy: no details
    InstalledAddon both; both.modId = 4; both.fileId = 6; both.adopted = true; both.fileName = "x.zip";
    InstalledAddon manual; manual.modId = 5; manual.fileId = 7; manual.manuallyProvided = true; manual.fileName = "m.zip";
    m.setAddons({adopted, linked, bare, both, manual});

    auto role = [&](int row, int r) { return m.data(m.index(row), r).toString(); };
    check(role(0, InstalledAddonsModel::DescriptionRole) == QString("Adopted from your AddOns folder \u00b7 Release \u00b7 flavor not set"),
          "an adopted addon's description says so and that its flavor is not set");
    check(!m.data(m.index(0), InstalledAddonsModel::LinkedRole).toBool() &&
          role(0, InstalledAddonsModel::VersionTextRole) == "Unknown until its first update" &&
          role(0, InstalledAddonsModel::SourceTextRole) == "Adopted from an existing folder",
          "an adopted addon is not linked, its version is unknown and its source says where it came from");
    check(role(1, InstalledAddonsModel::DescriptionRole) == QString("v9.3.2 \u00b7 Beta \u00b7 Retail \u00b7 Sep 29, 2026"),
          "a linked addon's description is version, channel, flavor and release date");
    check(m.data(m.index(1), InstalledAddonsModel::LinkedRole).toBool() && role(1, InstalledAddonsModel::ReleasedTextRole) == "Sep 29, 2026" &&
          role(1, InstalledAddonsModel::ChannelTextRole) == "Beta" && role(1, InstalledAddonsModel::ModSlugRole) == "ellesmere" &&
          role(1, InstalledAddonsModel::SourceTextRole) == "CurseForge download",
          "a linked addon exposes its details and is a CurseForge download");
    check(role(1, InstalledAddonsModel::InstalledTextRole).contains("2026") && role(0, InstalledAddonsModel::InstalledTextRole).contains("Sep"),
          "the install time is formatted");
    check(role(2, InstalledAddonsModel::DescriptionRole) == QString("Bare-1.zip \u00b7 Release"),
          "an addon recorded before details existed falls back to its zip name and leaves out what is unknown");
    check(role(3, InstalledAddonsModel::SourceTextRole) == "Adopted, linked to CurseForge", "an adopted addon that was linked says so");
    check(role(4, InstalledAddonsModel::SourceTextRole) == "Manual file", "a manually installed addon says so");

    check(role(1, InstalledAddonsModel::ShortVersionTextRole) == "v9.3.2" && role(0, InstalledAddonsModel::ShortVersionTextRole).isEmpty() &&
          role(2, InstalledAddonsModel::ShortVersionTextRole) == "Bare-1.zip",
          "the short version is the file's name, and empty for an adopted addon that is not linked");

    check(role(1, InstalledAddonsModel::ZipNameTextRole) == "EllesmereUI-v9.3.2.zip",
          "a linked addon shows its zip name after the version");
    check(role(0, InstalledAddonsModel::ZipNameTextRole).isEmpty() && role(2, InstalledAddonsModel::ZipNameTextRole).isEmpty() &&
          role(3, InstalledAddonsModel::ZipNameTextRole).isEmpty(),
          "no zip name when the version text already is the zip name, or the addon is adopted");

    m.setFlavorNames({{10, "Retail (Midnight)"}});
    check(role(1, InstalledAddonsModel::DescriptionRole).contains("Retail (Midnight)"), "a flavor rename shows in the description");

    check(m.fileIdFor(2) == 9006004 && m.fileIdFor(1) == 0 && m.fileIdFor(99) == 0, "fileIdFor finds an addon's file, or 0 when adopted or unknown");
    check(role(1, InstalledAddonsModel::ChangelogStateRole) == "none", "no changelog has been asked for yet");
    m.setChangelog(9006004, "loading", {});
    check(role(1, InstalledAddonsModel::ChangelogStateRole) == "loading" && role(2, InstalledAddonsModel::ChangelogStateRole) == "none",
          "a changelog's state belongs to its file only");
    m.setChangelog(9006004, "ready", "Major features");
    m.setAddons({linked}); // the list reloads after an update elsewhere
    check(role(0, InstalledAddonsModel::ChangelogTextRole) == "Major features" && m.changelogState(9006004) == "ready",
          "a loaded changelog survives a list reload");
    check(role(0, InstalledAddonsModel::ChangelogStateRole) == "ready", "and the row shows it");
}

void testChangelogsArePruned() {
    InstalledAddonsModel m;
    InstalledAddon a; a.modId = 1; a.fileId = 100; a.fileName = "a-1.zip";
    InstalledAddon b; b.modId = 2; b.fileId = 200; b.fileName = "b-1.zip";
    m.setAddons({a, b});
    m.setChangelog(100, "ready", "first");
    m.setChangelog(200, "ready", "other");
    check(m.changelogState(100) == "ready" && m.changelogState(200) == "ready", "changelogs are kept for installed files");

    a.fileId = 101; // addon 1 was updated: file 100 is gone
    m.setAddons({a, b});
    check(m.changelogState(100) == "none", "an updated addon's old changelog is dropped on reload");
    check(m.changelogState(200) == "ready", "an unchanged addon keeps its changelog on reload");

    m.setChangelog(100, "ready", "late reply"); // the request finished after the update
    check(m.changelogState(100) == "none", "a late changelog for a file that is no longer installed is ignored");
    m.setChangelog(101, "ready", "new");
    check(m.changelogState(101) == "ready", "a changelog for the installed file is stored");

    m.setAddons({});
    check(m.changelogState(101) == "none" && m.changelogState(200) == "none", "removing every addon drops every changelog");
}

void testFormatCount() {
    check(formatCount(0).isEmpty() && formatCount(-5).isEmpty(), "formatCount is empty for an unknown count");
    check(formatCount(842) == "842" && formatCount(999) == "999", "small counts are written out");
    check(formatCount(1000) == "1K" && formatCount(1500) == "1.5K" && formatCount(45300) == "45K" && formatCount(456000) == "456K",
          "thousands get a K, one decimal below 10K, never a trailing .0");
    check(formatCount(999999) == "1M" && formatCount(1234567) == "1.2M" && formatCount(12000000) == "12M",
          "millions get an M, and a count that rounds up to 1000K becomes 1M");
    check(formatCount(2500000000LL) == "2.5B", "billions get a B");
}

void testSearchResultsModel() {
    SearchResultsModel m;
    CurseForgeMod a; a.id = 10; a.name = "Alpha"; a.gameVersionTypeIds = {517, 67408};
    CurseForgeMod b; b.id = 20; b.name = "Beta";
    m.setResults({a, b});

    check(m.data(m.index(0), SearchResultsModel::DownloadsTextRole).toString().isEmpty() &&
          m.data(m.index(0), SearchResultsModel::UpdatedTextRole).toString().isEmpty() &&
          m.data(m.index(0), SearchResultsModel::VersionTextRole).toString().isEmpty(),
          "download, date and version roles are empty when the mod gave none");
    CurseForgeMod c; c.id = 30; c.name = "Gamma"; c.downloadCount = 1500; c.latestVersion = "v9.3.2"; c.latestDate = "2026-09-29T14:03:11.5Z";
    m.setResults({c});
    check(m.data(m.index(0), SearchResultsModel::DownloadsTextRole).toString() == "1.5K" &&
          m.data(m.index(0), SearchResultsModel::UpdatedTextRole).toString() == "Sep 29, 2026" &&
          m.data(m.index(0), SearchResultsModel::VersionTextRole).toString() == "v9.3.2",
          "a search result shows its download count, latest update date and latest version");
    m.setResults({a, b});
    check(m.flavorIdsFor(10) == QList<qint64>({517, 67408}), "flavorIdsFor returns the mod's flavors");
    check(m.flavorIdsFor(20).isEmpty(), "flavorIdsFor is empty for a mod that reports none");
    check(m.flavorIdsFor(999).isEmpty(), "flavorIdsFor is empty for an unknown mod");
}

// Spins the event loop until cond() holds or timeoutMs passes.
template <typename F>
bool waitFor(F cond, int timeoutMs = 2000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeoutMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

void writeText(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(content);
}

QByteArray readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// An addon zip with the given entries, as bytes. libzip reads the entry
// buffers only at zip_close, so `contents` must outlive it (same scope).
QByteArray zipBytes(const QString& path, const std::vector<std::pair<std::string, std::string>>& entries) {
    int err = 0;
    zip_t* z = zip_open(path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!z) return {};
    std::vector<std::string> contents;
    for (const auto& e : entries) contents.push_back(e.second);
    for (size_t i = 0; i < entries.size(); ++i)
        zip_file_add(z, entries[i].first.c_str(), zip_source_buffer(z, contents[i].data(), contents[i].size(), 0),
                     ZIP_FL_ENC_UTF_8);
    zip_close(z);
    return readText(path);
}

// Points the app at fresh config and data folders inside dir.
void useFreshHome(const QTemporaryDir& dir) {
    qputenv("XDG_CONFIG_HOME", (dir.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (dir.path() + "/data").toUtf8());
}

bool hasAddonFolder(const QString& wow, const char* folder) {
    return QDir(wow + "/Interface/AddOns/" + folder).exists();
}

// The selected profile's addon list on disk (where installed.json lived
// before profiles). Only after a controller has started: load() migrates.
StateStore profileState(qint64 id = 0) {
    const auto profiles = ProfileStore::load();
    return StateStore::load(profiles.statePath(id != 0 ? id : profiles.selectedId()));
}

QVariantMap profileMap(const WamController& c, qint64 id) {
    for (const auto& v : c.profiles())
        if (v.toMap().value("id").toLongLong() == id) return v.toMap();
    return {};
}

// Every key QML reads from wam.profiles, with the type it binds to.
bool profileMapsComplete(const WamController& c) {
    if (c.profiles().isEmpty()) return false;
    for (const auto& v : c.profiles()) {
        const auto m = v.toMap();
        const bool ok = m.value("id").typeId() == QMetaType::LongLong && m.value("name").typeId() == QMetaType::QString &&
                        m.value("active").typeId() == QMetaType::Bool && m.value("wowPath").typeId() == QMetaType::QString &&
                        m.value("flavorName").typeId() == QMetaType::QString && m.value("addonCount").typeId() == QMetaType::Int;
        if (!ok) return false;
    }
    return true;
}

// Stands in for CurseForge and its CDN. Answers the requests a test scripted
// by method and URL, records every request (the worker thread calls in), and
// fails any request nobody scripted, so a test never needs the real API.
struct FakeCurseForge {
    std::mutex m;
    std::map<std::string, HttpResponse> routes;
    std::vector<HttpRequest> seen;
    std::vector<std::string> unexpected;

    void on(const std::string& method, const std::string& url, HttpResponse r) { routes[method + " " + url] = std::move(r); }
    HttpResponse answer(const HttpRequest& r) {
        std::lock_guard<std::mutex> lock(m);
        seen.push_back(r);
        auto it = routes.find(r.method + " " + r.url);
        if (it == routes.end()) {
            unexpected.push_back(r.method + " " + r.url);
            return {404, R"({"error":"not scripted"})"};
        }
        return it->second;
    }
};

const std::string kApi = "https://api.curseforge.com/v1";

void testControllerEndToEnd() {
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", (tmp.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());
    const QString wow = tmp.path() + "/wow";
    // A setup from before profiles: the folder in config.json, the addons in installed.json.
    writeText(tmp.path() + "/config/wow-addon-manager/config.json", QString(R"({"wow_path":"%1"})").arg(wow).toUtf8());

    QDir().mkpath(tmp.path() + "/data/wow-addon-manager");
    QFile f(tmp.path() + "/data/wow-addon-manager/installed.json");
    check(f.open(QIODevice::WriteOnly), "test setup: created installed.json");
    f.write(R"({"addons":[{"modId":42,"fileId":1,"displayName":"Persisted","fileName":"p.zip","channel":1,)"
            R"("gameVersions":[],"folders":["Persisted"],"installedAt":"2026-01-01T00:00:00Z",)"
            R"("manuallyProvided":false,"flavorTypeId":517,"flavorName":"Retail"}]})");
    f.close();

    // A flavor cache from an earlier run, with one name the user edited.
    {
        wam::FlavorCache cache;
        cache.merge({{10, "Retail", "wow-retail"}, {40, "Forever", "wow-forever"}});
        cache.rename("wow-forever", "Ever");
        cache.save();
    }

    QString errContext, errMessage;
    {
        WamController c;
        QObject::connect(&c, &WamController::errorOccurred,
                         [&](const QString& ctx, const QString& msg) { errContext = ctx; errMessage = msg; });

        check(waitFor([&] { return c.installedAddons()->rowCount() == 1 && !c.profiles().isEmpty(); }),
              "state loaded on the worker thread reaches the GUI-thread model");
        const qint64 firstId = c.currentProfileId();
        const auto first = profileMap(c, firstId);
        check(c.profiles().size() == 1 && first.value("name") == "Default" && first.value("active").toBool() &&
              first.value("wowPath") == wow && first.value("addonCount") == 1 && c.currentProfileActive(),
              "first start makes one active profile from the old config and installed.json");
        check(profileMapsComplete(c), "every key QML reads from a profile is there, with its type");
        check(c.installedAddons()->data(c.installedAddons()->index(0),
                                        InstalledAddonsModel::FlavorNameRole).toString() == "Retail",
              "the stored flavor survives the cross-thread round trip");
        check(c.flavorEntries()->rowCount() == 2 && c.flavors().size() == 2,
              "flavors cached on disk are available at startup with no API key and no network");
        check(c.flavors()[1].toMap().value("name").toString() == "Ever", "the cached display name is what the flavor list shows");
        check(!c.hasApiKey() && c.hasWowPath() && c.wowPath() == wow, "no key, and the profile has the old config's folder");
        check(c.wowFlavorId() == 0 && c.wowFlavorName().isEmpty(), "no WoW flavor is known for a folder that names none");

        c.search("anything"); // no API key: must fail cleanly through the error signal, not crash or hang
        check(waitFor([&] { return errContext == "search"; }), "an API call without a key reports an error");

        // Scan + adopt end to end, offline: no API key, tagged folder on disk.
        QDir().mkpath(tmp.path() + "/wow/Interface/AddOns/Tagged");
        QFile toc(tmp.path() + "/wow/Interface/AddOns/Tagged/Tagged.toc");
        check(toc.open(QIODevice::WriteOnly), "test setup: created a .toc file");
        toc.write("## Title: |cff33ff99Tagged Addon|r\n## Version: 1.2\n## X-Curse-Project-ID: 777\n");
        toc.close();
        QDir().mkpath(tmp.path() + "/wow/Interface/AddOns/Loose");
        QDir().mkpath(tmp.path() + "/wow/Interface/AddOns/Persisted"); // already tracked: must not be listed

        // A second profile: selected at once, empty, on the same folder but
        // not active there, since the first profile's addons are in it.
        c.createProfile("Forever");
        check(waitFor([&] { return c.profiles().size() == 2 && c.currentProfileId() != firstId; }) &&
              c.installedAddons()->rowCount() == 0 && !c.currentProfileActive() && c.wowPath() == wow,
              "a new profile is selected, has no addons and is inactive on the shared folder");
        errContext.clear();
        c.scan();
        check(waitFor([&] { return !c.scanning(); }) && errContext == "scan" && c.scanResults()->rowCount() == 0,
              "scanning is refused on an inactive profile, whose folder holds another profile's addons");

        // A folder named after a cached flavor is recognised from the cache alone.
        QDir().mkpath(tmp.path() + "/wow_forever/_forever_");
        c.setWowPath(tmp.path() + "/wow_forever/_forever_");
        check(waitFor([&] { return c.wowFlavorId() == 40; }), "the WoW folder's flavor is worked out from the cache with no network");
        check(c.wowFlavorName() == "Ever", "and it is stored with its display name");
        c.renameFlavor("wow-forever", "Forever!");
        check(waitFor([&] { return c.wowFlavorName() == "Forever!"; }), "renaming a flavor updates the WoW folder's stored flavor name");
        check(waitFor([&] { return c.flavors()[1].toMap().value("name").toString() == "Forever!"; }), "and the flavor list");
        check(wam::FlavorCache::load().nameFor(40) == "Forever!", "and flavors.json on disk");
        c.setWowPath(wow);
        check(waitFor([&] { return !c.wowFlavorId() && c.wowPath() == wow; }), "a folder that names no flavor leaves it unknown");

        c.setWowFlavor(517); // a manual choice works without a key; the name lookup just comes back empty
        check(waitFor([&] { return c.wowFlavorId() == 517; }), "a flavor picked by hand is stored and reaches the controller");
        c.setWowPath(tmp.path() + "/elsewhere");
        check(waitFor([&] { return c.wowFlavorId() == 0; }), "changing the WoW path clears the flavor that described the old folder");

        c.selectProfile(firstId);
        check(waitFor([&] { return c.currentProfileId() == firstId && c.installedAddons()->rowCount() == 1; }) &&
              c.wowPath() == wow && c.currentProfileActive(),
              "switching back to the first profile shows its addons and folder again");

        c.scan();
        check(waitFor([&] { return !c.scanning(); }), "a scan finishes");
        check(c.scanResults()->folderCount() == 2 && c.scanResults()->rowCount() == 2,
              "scan lists untracked folders and skips tracked ones");
        check(c.scanResults()->matchedCount() == 1, "scan recognises the .toc-tagged folder");
        bool cleanedName = false, foundLoose = false;
        for (int i = 0; i < c.scanResults()->rowCount(); ++i) {
            const auto idx = c.scanResults()->index(i);
            if (c.scanResults()->data(idx, ScanResultsModel::ModIdRole).toLongLong() == 777)
                cleanedName = c.scanResults()->data(idx, ScanResultsModel::DetailsRole).toStringList()
                                  .value(0).contains("(Tagged Addon)") &&
                              !c.scanResults()->data(idx, ScanResultsModel::DetailsRole).toStringList()
                                  .value(0).contains("|c");
            else
                foundLoose = c.scanResults()->data(idx, ScanResultsModel::NameRole).toString() == "Loose";
        }
        check(cleanedName, "scan strips colour codes from .toc titles");
        check(foundLoose, "an untagged folder gets its own row named after the folder");

        c.adoptAllMatched();
        check(waitFor([&] { return c.installedAddons()->rowCount() == 2; }),
              "adopting a matched row adds it to the installed list");
        check(waitFor([&] { return c.scanResults()->rowCount() == 1; }),
              "the adopted folder leaves the scan results after the rescan");

        c.adopt(555, QStringList{"Loose"}, false);
        check(waitFor([&] { return c.installedAddons()->rowCount() == 3; }),
              "an untagged folder can be adopted with a hand-entered mod id");

        errContext.clear();
        c.adopt(556, QStringList{"Loose"}, false); // now owned by mod 555
        check(waitFor([&] { return errContext == "adopt"; }),
              "adopting a folder another mod owns is reported as an error");

        // Per-addon actions without an API key fail cleanly instead of hanging.
        errContext.clear();
        c.checkUpdate(42);
        check(waitFor([&] { return errContext == "checkUpdate"; }), "a single-addon update check without a key reports an error");
        errContext.clear();
        c.linkFile(42, 5, 517);
        check(waitFor([&] { return errContext == "link"; }), "linking without a key reports an error");
        c.loadChangelog(42);
        check(waitFor([&] { return c.installedAddons()->changelogState(1) == "failed"; }),
              "a changelog that cannot be fetched ends as failed, so the row can show it");
        c.loadChangelog(0); // not an installed addon: nothing to do

        // A state file that cannot be saved is reported, not an abort of the whole app.
        auto setDataWritable = [&](bool on) {
            const std::filesystem::path data = (tmp.path() + "/data").toStdString();
            const auto opt = on ? std::filesystem::perm_options::add : std::filesystem::perm_options::remove;
            std::filesystem::permissions(data, std::filesystem::perms::owner_write, opt);
            for (const auto& e : std::filesystem::recursive_directory_iterator(data))
                if (e.is_directory()) std::filesystem::permissions(e.path(), std::filesystem::perms::owner_write, opt);
        };
        if (::geteuid() != 0) { // root ignores the read-only folders (the Arch CI container)
            setDataWritable(false);
            errContext.clear();
            c.untrackAddon(555);
            check(waitFor([&] { return errContext == "untrackAddon"; }), "a failed save reports an error instead of aborting");
            setDataWritable(true);
            c.setFlavor(42, 517); // a later save must not write the untrack that failed
            errContext.clear();
            c.search("x"); // fails without a key; requests run in order, so setFlavor is done by then
            check(waitFor([&] { return errContext == "search"; }) && c.installedAddons()->rowCount() == 3,
                  "a failed untrack stays undone after a later save");
        }

        c.applyUpdate(42); // nothing queued for this mod: a no-op, not a crash
        c.skipUpdate(42);
        check(c.pendingUpdates()->count() == 0, "apply/skip on an unqueued mod is a no-op");
    } // ~WamController joins the worker thread
}

void testManualReinstallReplacesOldFolders() {
    // Offline, no API key: the path a user takes when the author blocks downloads.
    QTemporaryDir tmp;
    useFreshHome(tmp);
    const QString wow = tmp.path() + "/wow";
    writeText(tmp.path() + "/config/wow-addon-manager/config.json",
              QString(R"({"wow_path":"%1"})").arg(wow).toUtf8());
    QDir().mkpath(wow + "/Interface/AddOns");
    const QString v1 = tmp.path() + "/v1.zip", v2 = tmp.path() + "/v2.zip";
    zipBytes(v1, {{"Mine/Mine.toc", "## Title: Mine\n"}, {"MineExtra/MineExtra.toc", "## Title: Extra\n"}});
    zipBytes(v2, {{"Mine/Mine.toc", "## Title: Mine v2\n"}});

    WamController c;
    c.installManual(60, 600, v1);
    check(waitFor([&] { return c.installedAddons()->fileIdFor(60) == 600; }) &&
          hasAddonFolder(wow, "Mine") && hasAddonFolder(wow, "MineExtra"),
          "a manual install lays down every folder in the zip and is tracked");
    c.installManual(60, 601, v2);
    check(waitFor([&] { return c.installedAddons()->fileIdFor(60) == 601; }) &&
          !hasAddonFolder(wow, "MineExtra") && readText(wow + "/Interface/AddOns/Mine/Mine.toc").contains("v2"),
          "a manual reinstall replaces the old version, including a folder it no longer ships");
    const auto rec = profileState().find(60);
    check(rec && rec->folders == std::vector<std::string>({"Mine"}) && rec->manuallyProvided,
          "installed.json records only the new version's folders, as a manual file");
}

void testUpdatesWithFakeCurseForge() {
    QTemporaryDir tmp;
    useFreshHome(tmp);
    const QString wow = tmp.path() + "/wow";
    const QString addons = wow + "/Interface/AddOns";
    writeText(tmp.path() + "/config/wow-addon-manager/config.json",
              QString(R"({"curseforge_api_key":"test-key","wow_path":"%1","wow_flavor_id":517,"wow_flavor_name":"Retail"})")
                  .arg(wow).toUtf8());
    writeText(tmp.path() + "/data/wow-addon-manager/installed.json", R"({"addons":[
        {"modId":42,"fileId":100,"displayName":"Foo","fileName":"foo-100.zip","channel":1,"folders":["Foo","FooOptions"],"flavorTypeId":517,"flavorName":"Retail"},
        {"modId":43,"fileId":300,"displayName":"Bar","fileName":"bar-300.zip","channel":1,"folders":["Bar"],"flavorTypeId":517,"flavorName":"Retail"}]})");
    writeText(addons + "/Foo/core.lua", "v1\n");
    writeText(addons + "/FooOptions/options.lua", "options\n");
    writeText(addons + "/Bar/bar.lua", "bar v1\n");

    auto json = [](const std::string& body) { return HttpResponse{200, body}; };
    FakeCurseForge fake;
    fake.on("GET", kApi + "/games/1/version-types", json(R"({"data":[{"id":517,"name":"Retail","slug":"wow-retail"}]})"));
    // Startup fills in icons, authors and file details for tracked addons.
    fake.on("POST", kApi + "/mods", json(R"({"data":[{"id":42,"name":"Foo","slug":"foo"},{"id":43,"name":"Bar","slug":"bar"}]})"));
    fake.on("POST", kApi + "/mods/files", json(R"({"data":[{"id":100,"modId":42,"fileName":"foo-100.zip"},{"id":300,"modId":43,"fileName":"bar-300.zip"}]})"));
    fake.on("GET", kApi + "/mods/42", json(R"({"data":{"id":42,"name":"Foo","slug":"foo"}})"));
    fake.on("GET", kApi + "/mods/43", json(R"({"data":{"id":43,"name":"Bar","slug":"bar"}})"));
    fake.on("GET", kApi + "/mods/44", json(R"({"data":{"id":44,"name":"Baz","slug":"baz"}})"));
    // The newest file overall is a beta: a release-channel addon must not be offered it.
    fake.on("GET", kApi + "/mods/42/files?pageSize=50&index=0&gameVersionTypeId=517", json(R"({"data":[
        {"id":100,"modId":42,"fileName":"foo-100.zip","releaseType":1,"downloadUrl":"u"},
        {"id":102,"modId":42,"fileName":"foo-102.zip","releaseType":1,"downloadUrl":"u"},
        {"id":103,"modId":42,"fileName":"foo-103-beta.zip","releaseType":2,"downloadUrl":"u"}]})"));
    fake.on("GET", kApi + "/mods/43/files?pageSize=50&index=0&gameVersionTypeId=517", json(R"({"data":[
        {"id":300,"modId":43,"fileName":"bar-300.zip","releaseType":1,"downloadUrl":"u"},
        {"id":301,"modId":43,"fileName":"bar-301.zip","releaseType":1,"downloadUrl":"u"}]})"));
    fake.on("GET", kApi + "/mods/42/files/102", json(R"({"data":{"id":102,"modId":42,"fileName":"foo-102.zip","releaseType":1,
        "downloadUrl":"https://edge.forgecdn.net/files/1/102/foo-102.zip"}})"));
    fake.on("GET", kApi + "/mods/43/files/301", json(R"({"data":{"id":301,"modId":43,"fileName":"bar-301.zip","releaseType":1,
        "downloadUrl":"https://edge.forgecdn.net/files/1/301/bar-301.zip"}})"));
    fake.on("GET", kApi + "/mods/44/files/440", json(R"({"data":{"id":440,"modId":44,"fileName":"baz-440.zip","releaseType":1,
        "downloadUrl":null}})"));
    fake.on("DOWNLOAD", "https://edge.forgecdn.net/files/1/102/foo-102.zip",
            HttpResponse{200, zipBytes(tmp.path() + "/foo-102.zip", {{"Foo/core.lua", "v2\n"}}).toStdString()});
    fake.on("DOWNLOAD", "https://edge.forgecdn.net/files/1/301/bar-301.zip", HttpResponse{500, ""});
    HttpClient::transportForTesting = [&fake](const HttpRequest& r) { return fake.answer(r); };

    {
        WamController c;
        int blocked = 0;
        QObject::connect(&c, &WamController::downloadBlocked, [&] { ++blocked; });
        check(waitFor([&] { return c.installedAddons()->rowCount() == 2; }), "test setup: two tracked addons load");

        c.checkAllUpdates();
        check(waitFor([&] { return !c.checkingUpdates(); }) && c.pendingUpdates()->count() == 2 &&
              c.pendingUpdates()->find(42) && c.pendingUpdates()->find(42)->latestFileId == 102,
              "an update check offers the newest file in the addon's channel and flavor, not a newer beta");

        c.applyUpdate(42);
        check(waitFor([&] { return c.installedAddons()->fileIdFor(42) == 102; }) &&
              readText(addons + "/Foo/core.lua") == "v2\n" && !hasAddonFolder(wow, "FooOptions"),
              "applying an update installs the new files and removes the folder the new version dropped");
        const auto foo = profileState().find(42);
        check(foo && foo->fileId == 102 && foo->folders == std::vector<std::string>({"Foo"}) && !c.pendingUpdates()->find(42),
              "the applied update is saved to installed.json and leaves the queue");

        c.applyUpdate(43);
        check(waitFor([&] { const auto* u = c.pendingUpdates()->find(43); return u && u->status == PendingUpdate::Status::Failed; }) &&
              readText(addons + "/Bar/bar.lua") == "bar v1\n" && profileState().find(43)->fileId == 300,
              "a failed download leaves the installed addon and its record as they were");

        // Blocked download, then the user supplies the zip: the flavor chosen at install is kept.
        c.installFile(44, 440, 517);
        check(waitFor([&] { return blocked == 1; }), "a file whose author blocks downloads is reported, not installed");
        const QString baz = tmp.path() + "/baz.zip";
        zipBytes(baz, {{"Baz/Baz.toc", "## Title: Baz\n"}});
        c.installManual(44, 440, baz);
        check(waitFor([&] { return c.installedAddons()->fileIdFor(44) == 440; }), "the manual zip installs");
        const auto bazRec = profileState().find(44);
        check(bazRec && bazRec->flavorTypeId == 517 && bazRec->manuallyProvided && hasAddonFolder(wow, "Baz"),
              "a manual install after a blocked download keeps the flavor picked for it");
    } // joins the worker before the fake goes away

    HttpClient::transportForTesting = [](const HttpRequest& r) { return HttpResponse{0, "network is off in tests: " + r.url}; };
    bool keyOnlyToApi = !fake.seen.empty();
    for (const auto& r : fake.seen) {
        const bool api = r.url.rfind(kApi + "/", 0) == 0;
        const bool sendsKey = std::find(r.headers.begin(), r.headers.end(), "x-api-key: test-key") != r.headers.end();
        const bool mentionsKey = std::any_of(r.headers.begin(), r.headers.end(),
                                             [](const std::string& h) { return h.find("test-key") != std::string::npos; });
        if (api ? !sendsKey : mentionsKey) keyOnlyToApi = false;
    }
    check(keyOnlyToApi, "the API key goes with every CurseForge API request and never to the download host");
    for (const auto& u : fake.unexpected) std::cerr << "unscripted request: " << u << "\n";
    check(fake.unexpected.empty(), "the app made no request the fake CurseForge was not scripted for");
}

// Installs into an inactive profile, then switches the WoW folder over to it
// and back, against a fake CurseForge.
void testProfilesWithFakeCurseForge() {
    QTemporaryDir tmp;
    useFreshHome(tmp);
    const QString wow = tmp.path() + "/wow";
    const QString lib = tmp.path() + "/data/wow-addon-manager/library";
    writeText(tmp.path() + "/config/wow-addon-manager/config.json",
              QString(R"({"curseforge_api_key":"test-key","wow_path":"%1","wow_flavor_id":517,"wow_flavor_name":"Retail"})")
                  .arg(wow).toUtf8());
    writeText(tmp.path() + "/data/wow-addon-manager/installed.json", R"({"addons":[
        {"modId":42,"fileId":100,"displayName":"Foo","fileName":"foo-100.zip","channel":1,"folders":["Foo"],"flavorTypeId":517,"flavorName":"Retail"}]})");
    writeText(wow + "/Interface/AddOns/Foo/core.lua", "foo v1\n");

    auto json = [](const std::string& body) { return HttpResponse{200, body}; };
    FakeCurseForge fake;
    fake.on("GET", kApi + "/games/1/version-types", json(R"({"data":[{"id":517,"name":"Retail","slug":"wow-retail"}]})"));
    fake.on("POST", kApi + "/mods", json(R"({"data":[{"id":42,"name":"Foo","slug":"foo"},{"id":50,"name":"New","slug":"new"}]})"));
    fake.on("POST", kApi + "/mods/files", json(R"({"data":[{"id":100,"modId":42,"fileName":"foo-100.zip"},{"id":500,"modId":50,"fileName":"new-500.zip"}]})"));
    fake.on("GET", kApi + "/mods/42", json(R"({"data":{"id":42,"name":"Foo","slug":"foo"}})"));
    fake.on("GET", kApi + "/mods/42/files?pageSize=50&index=0&gameVersionTypeId=517", json(R"({"data":[
        {"id":101,"modId":42,"fileName":"foo-101.zip","releaseType":1,"downloadUrl":"u"}]})"));
    fake.on("GET", kApi + "/mods/50", json(R"({"data":{"id":50,"name":"New","slug":"new"}})"));
    fake.on("GET", kApi + "/mods/50/files/500", json(R"({"data":{"id":500,"modId":50,"fileName":"new-500.zip","releaseType":1,
        "downloadUrl":"https://edge.forgecdn.net/files/1/500/new-500.zip"}})"));
    fake.on("DOWNLOAD", "https://edge.forgecdn.net/files/1/500/new-500.zip",
            HttpResponse{200, zipBytes(tmp.path() + "/new-500.zip", {{"New/New.toc", "## Title: New\n"}}).toStdString()});
    HttpClient::transportForTesting = [&fake](const HttpRequest& r) { return fake.answer(r); };
    auto downloads = [&fake] {
        std::lock_guard<std::mutex> lock(fake.m);
        return std::count_if(fake.seen.begin(), fake.seen.end(), [](const HttpRequest& r) { return r.method == "DOWNLOAD"; });
    };

    {
        WamController c;
        QString activatedName;
        QString errContext;
        QObject::connect(&c, &WamController::profileActivated, [&](qint64, const QString& name) { activatedName = name; });
        QObject::connect(&c, &WamController::errorOccurred, [&](const QString& ctx, const QString&) { errContext = ctx; });
        check(waitFor([&] { return c.installedAddons()->rowCount() == 1 && !c.profiles().isEmpty(); }),
              "test setup: the migrated profile loads");
        const qint64 firstId = c.currentProfileId();

        // An update check still running when another profile is selected.
        c.checkAllUpdates();
        c.createProfile("Raid");
        check(waitFor([&] { return c.currentProfileId() != firstId && !c.checkingUpdates(); }) &&
              c.pendingUpdates()->count() == 0,
              "an update check that finishes after switching profiles leaves nothing in the new profile's queue");
        const qint64 raidId = c.currentProfileId();

        c.installFile(50, 500, 517);
        check(waitFor([&] { return c.installedAddons()->fileIdFor(50) == 500; }) &&
              !hasAddonFolder(wow, "New") && hasAddonFolder(wow, "Foo") && QFile::exists(lib + "/50/500.zip") &&
              profileState(raidId).find(50).value_or(InstalledAddon{}).folders == std::vector<std::string>({"New"}),
              "installing into an inactive profile lists it there and stores the zip, leaving AddOns/ untouched");

        c.activateProfile(raidId);
        check(c.activatingProfile(), "activatingProfile is set as soon as a switch is asked for");
        check(waitFor([&] { return !activatedName.isEmpty(); }) && activatedName == "Raid" &&
              hasAddonFolder(wow, "New") && !hasAddonFolder(wow, "Foo") && c.currentProfileActive() &&
              !profileMap(c, firstId).value("active").toBool(),
              "activating a profile swaps AddOns/ to its addons and reports it");
        check(waitFor([&] { return !c.activatingProfile(); }), "activatingProfile clears when the switch is done");

        // Back again: Foo was never downloaded, so it comes from the snapshot
        // taken when the folder switched away from it.
        activatedName.clear();
        c.selectProfile(firstId);
        c.activateProfile(firstId);
        check(waitFor([&] { return !activatedName.isEmpty(); }) && hasAddonFolder(wow, "Foo") &&
              readText(wow + "/Interface/AddOns/Foo/core.lua") == "foo v1\n" && !hasAddonFolder(wow, "New") &&
              downloads() == 1,
              "switching back restores the first profile's addons from the library, with no download");

        // A zip gone from the library is fetched again by file id.
        QFile::remove(lib + "/50/500.zip");
        activatedName.clear();
        c.activateProfile(raidId);
        check(waitFor([&] { return !activatedName.isEmpty(); }) && hasAddonFolder(wow, "New") && downloads() == 2,
              "a switch downloads a zip the library lost, by its file id");
        check(waitFor([&] { return !c.activatingProfile(); }) && errContext.isEmpty(), "no install or switch above reported an error");
    }

    HttpClient::transportForTesting = [](const HttpRequest& r) { return HttpResponse{0, "network is off in tests: " + r.url}; };
    for (const auto& u : fake.unexpected) std::cerr << "unscripted request: " << u << "\n";
    check(fake.unexpected.empty(), "profiles made no request the fake CurseForge was not scripted for");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Never the user's real config (it holds their API key) or state, and
    // never the network: any request a test did not script fails.
    QTemporaryDir home;
    useFreshHome(home);
    HttpClient::transportForTesting = [](const HttpRequest& r) { return HttpResponse{0, "network is off in tests: " + r.url}; };

    testPendingUpdatesModel();
    testScanResultsModel();
    testCleanTocText();
    testInstallFilesModel();
    testInstallFileFromCurseForge();
    testFlavorsModel();
    testInstalledFlavorNames();
    testInstalledRowTexts();
    testChangelogsArePruned();
    testFormatCount();
    testSearchResultsModel();
    testControllerEndToEnd();
    testManualReinstallReplacesOldFolders();
    testUpdatesWithFakeCurseForge();
    testProfilesWithFakeCurseForge();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
