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
#include "core/flavor_cache.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>

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
    int changes = 0;
    QObject::connect(&m, &PendingUpdatesModel::changed, [&] { ++changes; });

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
    check(changes > 0, "changed() fires on every mutation");

    m.clear();
    check(m.count() == 0 && m.head().value("latestFile").toString().isEmpty() && m.head().contains("iconUrl"),
          "clear() empties the queue and the head falls back to defaults");
}

void testScanResultsModel() {
    ScanResultsModel m;
    ScanGroup tagged; tagged.modId = 3358; tagged.name = "Deadly Boss Mods"; tagged.iconUrl = "http://i";
    tagged.folders = {"DBM-Core", "DBM-GUI"}; tagged.details = {"DBM-Core v1", "DBM-GUI"};
    ScanGroup loose; loose.name = "My Hand-Made Addon"; loose.folders = {"MyAddon"}; loose.details = {"MyAddon"};
    m.setGroups({tagged, loose});

    check(m.rowCount() == 2, "scan model holds one row per group");
    check(m.folderCount() == 3, "folderCount totals folders across rows");
    check(m.matchedCount() == 1, "matchedCount counts only .toc-tagged rows");
    check(m.data(m.index(0), ScanResultsModel::MatchedRole).toBool() &&
          !m.data(m.index(1), ScanResultsModel::MatchedRole).toBool(),
          "matched role distinguishes tagged from untagged rows");
    check(m.data(m.index(0), ScanResultsModel::FoldersRole).toStringList() == QStringList({"DBM-Core", "DBM-GUI"}),
          "folders role carries every folder in the row");
    check(m.data(m.index(0), ScanResultsModel::IconUrlRole).toString() == "http://i", "icon role");
    check(m.groups().size() == 2, "groups() returns a copy for bulk adoption");
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

void testSearchResultsModel() {
    SearchResultsModel m;
    CurseForgeMod a; a.id = 10; a.name = "Alpha"; a.gameVersionTypeIds = {517, 67408}; a.logoUrl = "http://logo";
    CurseForgeMod b; b.id = 20; b.name = "Beta";
    m.setResults({a, b});

    check(m.rowCount() == 2, "search results model holds the results");
    check(m.data(m.index(0), SearchResultsModel::NameRole).toString() == "Alpha", "name role");
    check(m.data(m.index(0), SearchResultsModel::LogoUrlRole).toString() == "http://logo", "logoUrl role");
    check(m.flavorIdsFor(10) == QList<qint64>({517, 67408}), "flavorIdsFor returns the mod's flavors");
    check(m.flavorIdsFor(20).isEmpty(), "flavorIdsFor is empty for a mod that reports none");
    check(m.flavorIdsFor(999).isEmpty(), "flavorIdsFor is empty for an unknown mod");
}

void testInstalledAddonsModel() {
    InstalledAddonsModel m;
    InstalledAddon a;
    a.modId = 5; a.displayName = "Foo"; a.flavorTypeId = 517; a.flavorName = "Retail"; a.iconUrl = "http://icon";
    a.folders = {"Foo", "FooOptions"};
    InstalledAddon legacy;
    legacy.modId = 6; legacy.displayName = "Bar"; // flavor unknown
    m.setAddons({a, legacy});

    check(m.rowCount() == 2, "installed model holds the addons");
    check(m.data(m.index(0), InstalledAddonsModel::FlavorNameRole).toString() == "Retail", "flavorName role");
    check(m.data(m.index(0), InstalledAddonsModel::FlavorTypeIdRole).toLongLong() == 517, "flavorTypeId role");
    check(m.data(m.index(0), InstalledAddonsModel::IconUrlRole).toString() == "http://icon", "iconUrl role");
    check(m.data(m.index(1), InstalledAddonsModel::FlavorTypeIdRole).toLongLong() == 0,
          "an addon with no recorded flavor reports flavorTypeId 0");
    check(m.data(m.index(0), InstalledAddonsModel::FoldersRole).toStringList().size() == 2, "folders role");
}

// Spins the event loop until cond() holds or timeoutMs passes.
template <typename F>
bool waitFor(F cond, int timeoutMs = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeoutMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

void testControllerEndToEnd() {
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", (tmp.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());

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

        check(waitFor([&] { return c.installedAddons()->rowCount() == 1; }),
              "state loaded on the worker thread reaches the GUI-thread model");
        check(c.installedAddons()->data(c.installedAddons()->index(0),
                                        InstalledAddonsModel::FlavorNameRole).toString() == "Retail",
              "the stored flavor survives the cross-thread round trip");
        check(c.flavorEntries()->rowCount() == 2 && c.flavors().size() == 2,
              "flavors cached on disk are available at startup with no API key and no network");
        check(c.flavors()[1].toMap().value("name").toString() == "Ever", "the cached display name is what the flavor list shows");
        check(!c.hasApiKey() && !c.hasWowPath(), "no key or path configured on a fresh config");
        check(c.wowFlavorId() == 0 && c.wowFlavorName().isEmpty(), "no WoW flavor is known on a fresh config");

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

        c.setWowPath(tmp.path() + "/wow");
        check(waitFor([&] { return c.hasWowPath(); }), "setting the WoW path reaches the controller");
        check(c.wowFlavorId() == 0, "without an API key the flavor cannot be worked out, and stays unknown");

        // A folder named after a cached flavor is recognised from the cache alone.
        QDir().mkpath(tmp.path() + "/wow_forever/_forever_");
        c.setWowPath(tmp.path() + "/wow_forever/_forever_");
        check(waitFor([&] { return c.wowFlavorId() == 40; }), "the WoW folder's flavor is worked out from the cache with no network");
        check(c.wowFlavorName() == "Ever", "and it is stored with its display name");
        c.renameFlavor("wow-forever", "Forever!");
        check(waitFor([&] { return c.wowFlavorName() == "Forever!"; }), "renaming a flavor updates the WoW folder's stored flavor name");
        check(waitFor([&] { return c.flavors()[1].toMap().value("name").toString() == "Forever!"; }), "and the flavor list");
        check(wam::FlavorCache::load().nameFor(40) == "Forever!", "and flavors.json on disk");
        c.setWowPath(tmp.path() + "/wow");
        check(waitFor([&] { return !c.wowFlavorId() && c.hasWowPath(); }), "a folder that names no flavor leaves it unknown");

        c.setWowFlavor(517); // a manual choice works without a key; the name lookup just comes back empty
        check(waitFor([&] { return c.wowFlavorId() == 517; }), "a flavor picked by hand is stored and reaches the controller");
        c.setWowPath(tmp.path() + "/wow");
        check(waitFor([&] { return c.wowFlavorId() == 0; }), "changing the WoW path clears the flavor that described the old folder");

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
        check(c.downloadUrl(26886, 8875044) == "https://www.curseforge.com/api/v1/mods/26886/files/8875044/download",
              "downloadUrl gives the website's direct download link");
        check(c.modPageUrl("questie") == "https://www.curseforge.com/wow/addons/questie", "modPageUrl gives the addon page");

        c.applyUpdate(42); // nothing queued for this mod: a no-op, not a crash
        c.skipUpdate(42);
        check(c.pendingUpdates()->count() == 0, "apply/skip on an unqueued mod is a no-op");
    } // ~WamController joins the worker thread
    check(true, "controller shuts its worker thread down cleanly");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testPendingUpdatesModel();
    testScanResultsModel();
    testCleanTocText();
    testInstallFilesModel();
    testInstallFileFromCurseForge();
    testFlavorsModel();
    testInstalledFlavorNames();
    testInstalledRowTexts();
    testSearchResultsModel();
    testInstalledAddonsModel();
    testControllerEndToEnd();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
