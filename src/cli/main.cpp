#include "core/config.hpp"
#include "core/curseforge_client.hpp"
#include "core/addon_installer.hpp"
#include "core/state_store.hpp"
#include "core/http_client.hpp"
#include "core/scoped_temp_file.hpp"
#include "core/toc_reader.hpp"
#include "core/reconciler.hpp"
#include "core/flavor_cache.hpp"

#include <iostream>
#include <sstream>
#include <optional>
#include <algorithm>
#include <unordered_set>
#include <cstdlib>

using namespace wam;

namespace {

void printUsage() {
    std::cout <<
        "wam - WoW addon manager core CLI\n\n"
        "Config:\n"
        "  wam config set-key <api_key>\n"
        "  wam config set-path <wow_install_dir>\n"
        "  wam config show\n\n"
        "CurseForge:\n"
        "  wam search <query>\n"
        "  wam flavors                       (list known WoW flavors/version-types)\n"
        "  wam files <modId> [--flavor <name>]\n"
        "  wam install <modId> [--channel release|beta|alpha] [--flavor <name>]\n\n"
        "Updates (shows current-vs-latest and asks before applying; --yes skips the prompt).\n"
        "An addon updates within the flavor it was installed for; --flavor overrides and re-saves it:\n"
        "  wam update <modId> [--channel release|beta|alpha] [--flavor <name>] [--yes]\n"
        "  wam update-all [--flavor <name>] [--yes]\n\n"
        "Manual install (for addons with third-party downloads blocked):\n"
        "  wam install-manual <modId> <fileId> <local-zip-path>\n\n"
        "Reconciliation (folders already in AddOns/ that wam didn't put there):\n"
        "  wam scan                                        (report untracked folders + .toc-detected mod ids)\n"
        "  wam adopt --mod-id <id>                         (adopt all untracked folders tagged for that mod)\n"
        "  wam adopt --folder <name> --mod-id <id>         (manually assign one folder, e.g. no .toc tag found)\n"
        "  wam set-flavor <modId> <flavor>                 (record which flavor an adopted addon is for)\n\n"
        "Local state:\n"
        "  wam list\n"
        "  wam remove <modId>              (delete tracked folders from disk + drop from state)\n"
        "  wam untrack <modId>             (drop from state only: files untouched, for re-scanning)\n";
}

ReleaseChannel parseChannel(const std::string& s) {
    if (s == "release") return ReleaseChannel::Release;
    if (s == "beta") return ReleaseChannel::Beta;
    if (s == "alpha") return ReleaseChannel::Alpha;
    throw std::runtime_error("unknown channel '" + s + "' (expected release|beta|alpha)");
}

std::string channelName(ReleaseChannel c) {
    switch (c) {
        case ReleaseChannel::Release: return "release";
        case ReleaseChannel::Beta: return "beta";
        case ReleaseChannel::Alpha: return "alpha";
    }
    return "unknown";
}

CurseForgeClient requireClient() {
    auto cfg = Config::load();
    if (!cfg.curseforge_api_key.has_value())
        throw std::runtime_error("no CurseForge API key configured, run: wam config set-key <key>");
    return CurseForgeClient(*cfg.curseforge_api_key);
}

// Resolves the AddOns dir and, if a previous install was killed mid-swap,
// puts the interrupted folders back before anything else looks at them.
std::filesystem::path requireAddonsDir() {
    auto cfg = Config::load();
    auto dir = cfg.addonsDir(); // throws if wow_path unset
    if (std::filesystem::exists(dir)) {
        auto restored = AddonInstaller::recoverInterrupted(dir);
        if (restored > 0)
            std::cerr << "(recovered " << restored << " folder(s) from an interrupted install)\n";
    }
    return dir;
}

std::string flavorNameFor(CurseForgeClient& client, int64_t id) {
    if (id == 0) return {};
    try {
        for (const auto& t : client.listGameVersionTypes())
            if (t.id == id) return t.name;
    } catch (const std::exception&) {}
    return {};
}

// Prints "unknown flavor" + the valid list. Returns the resolved id or nullopt.
std::optional<int64_t> resolveFlavorOrComplain(CurseForgeClient& client, const std::string& flavor,
                                                const std::string& prefix = "") {
    auto id = client.gameVersionTypeId(flavor);
    if (!id.has_value()) {
        std::cerr << prefix << "Unknown flavor '" << flavor << "'. Known flavors:\n";
        try {
            for (const auto& t : client.listGameVersionTypes()) std::cerr << "  " << t.name << "\n";
        } catch (const std::exception&) {}
    }
    return id;
}

std::string joinFolders(const std::vector<std::string>& folders) {
    std::string out;
    for (size_t i = 0; i < folders.size(); ++i) out += folders[i] + (i + 1 < folders.size() ? ", " : "");
    return out;
}

// Records the install in state and prints a verification summary.
void recordAndReport(const InstalledAddon& addon) {
    auto store = StateStore::load();
    store.upsert(addon);
    store.save();

    std::cout << "Installed: " << addon.displayName << " (mod " << addon.modId
              << ", file " << addon.fileId << ")\n"
              << "  Channel: " << channelName(addon.channel) << "\n";
    if (!addon.flavorName.empty()) std::cout << "  Flavor:  " << addon.flavorName << "\n";
    std::cout << "  Folders: " << joinFolders(addon.folders)
              << "\n  Source:  " << (addon.manuallyProvided ? "manual file" : "CurseForge download") << "\n";
}

int cmdConfig(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    std::string sub = argv[0];
    auto cfg = Config::load();

    if (sub == "set-key" && argc >= 2) {
        cfg.curseforge_api_key = argv[1];
        cfg.save();
        std::cout << "CurseForge API key saved to " << Config::configPath() << "\n";
        return 0;
    }
    if (sub == "set-path" && argc >= 2) {
        cfg.wow_path = argv[1];
        cfg.save();
        std::cout << "WoW install path set to " << *cfg.wow_path << "\n";
        std::cout << "AddOns folder resolves to: " << cfg.addonsDir() << "\n";
        return 0;
    }
    if (sub == "show") {
        std::cout << "Config file: " << Config::configPath() << "\n";
        std::cout << "  curseforge_api_key: " << (cfg.curseforge_api_key ? "(set)" : "(not set)") << "\n";
        std::cout << "  wow_path: " << (cfg.wow_path ? *cfg.wow_path : "(not set)") << "\n";
        return 0;
    }
    printUsage();
    return 1;
}

int cmdSearch(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    auto client = requireClient();
    auto results = client.search(argv[0]);

    std::cout << "Found " << results.size() << " addon(s) for \"" << argv[0] << "\":\n";
    for (const auto& m : results) {
        std::cout << "  [" << m.id << "] " << m.name;
        if (!m.slug.empty()) std::cout << " (" << m.slug << ")";
        std::cout << "\n";
        if (!m.summary.empty()) std::cout << "      " << m.summary << "\n";
    }
    return 0;
}

int cmdFlavors(int, char**) {
    auto client = requireClient();
    std::cout << "Known WoW flavors (from CurseForge):\n";
    for (const auto& t : client.listGameVersionTypes()) {
        std::cout << "  [" << t.id << "] " << t.name;
        if (!t.slug.empty()) std::cout << " (" << t.slug << ")";
        std::cout << "\n";
    }
    return 0;
}

int cmdFiles(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);
    std::optional<std::string> flavor;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--flavor" && i + 1 < argc) flavor = argv[++i];
    }

    auto client = requireClient();
    std::optional<int64_t> gvTypeId;
    if (flavor.has_value()) {
        gvTypeId = resolveFlavorOrComplain(client, *flavor);
        if (!gvTypeId.has_value()) return 1;
    }
    auto files = client.getFiles(modId, gvTypeId);

    std::cout << "Found " << files.size() << " file(s) for mod " << modId << ":\n";
    for (const auto& f : files) {
        std::cout << "  [" << f.id << "] " << f.displayName
                   << " (" << channelName(f.releaseType) << ")"
                   << (f.isBlocked() ? " [THIRD-PARTY DOWNLOAD BLOCKED]" : "") << "\n";
        std::cout << "      versions: ";
        for (size_t i = 0; i < f.gameVersions.size(); ++i)
            std::cout << f.gameVersions[i] << (i + 1 < f.gameVersions.size() ? ", " : "");
        std::cout << "\n";
    }
    return 0;
}

int cmdInstall(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);
    ReleaseChannel channel = ReleaseChannel::Release;
    std::optional<std::string> flavor;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--channel" && i + 1 < argc) channel = parseChannel(argv[++i]);
        else if (a == "--flavor" && i + 1 < argc) flavor = argv[++i];
    }

    auto client = requireClient();
    auto mod = client.getMod(modId);

    std::optional<int64_t> gvTypeId;
    if (flavor.has_value()) {
        gvTypeId = resolveFlavorOrComplain(client, *flavor);
        if (!gvTypeId.has_value()) return 1;
    }

    auto files = client.getFiles(modId, gvTypeId);
    auto chosen = CurseForgeClient::selectBestFile(files, channel);

    if (!chosen.has_value()) {
        std::cerr << "No " << channelName(channel) << " file found for '" << mod.name << "'"
                   << (flavor ? " matching flavor '" + *flavor + "'" : "") << "\n";
        return 1;
    }

    if (chosen->isBlocked()) {
        std::cerr << "'" << mod.name << "' (file " << chosen->id << ", " << chosen->fileName << ") "
                   << "has third-party downloads blocked by the author.\n"
                   << "Download it in your browser from " << CurseForgeClient::browserDownloadUrl(modId, chosen->id)
                   << " and then run:\n"
                   << "  wam install-manual " << modId << " " << chosen->id << " <path-to-downloaded-zip>\n";
        return 2;
    }

    auto addonsDir = requireAddonsDir();
    ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(chosen->id) + "_" + chosen->fileName));
    auto dlResp = HttpClient::downloadToFile(*chosen->downloadUrl, tmpZip.path());
    if (!dlResp.ok()) {
        std::cerr << "Download failed (HTTP " << dlResp.status << ")\n";
        return 1;
    }

    // Reinstalling an already-tracked mod replaces its old folders atomically.
    auto store = StateStore::load();
    auto existing = store.find(modId);
    auto folders = AddonInstaller::installZip(tmpZip.path(), addonsDir,
                                              existing ? existing->folders : std::vector<std::string>{});

    InstalledAddon rec;
    rec.modId = modId;
    rec.displayName = mod.name;
    recordFile(rec, *chosen);
    rec.modSlug = mod.slug;
    rec.folders = folders;
    rec.installedAt = nowIso8601();
    rec.manuallyProvided = false;
    rec.iconUrl = !mod.logoUrl.empty() ? mod.logoUrl : (existing ? existing->iconUrl : std::string());
    if (gvTypeId.has_value()) {
        rec.flavorTypeId = *gvTypeId;
        rec.flavorName = flavorNameFor(client, *gvTypeId);
    } else if (existing.has_value()) {
        rec.flavorTypeId = existing->flavorTypeId;
        rec.flavorName = existing->flavorName;
    }

    recordAndReport(rec);
    return 0;
}

int cmdInstallManual(int argc, char** argv) {
    if (argc < 3) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);
    int64_t fileId = std::stoll(argv[1]);
    std::filesystem::path zipPath = argv[2];

    if (!std::filesystem::exists(zipPath)) {
        std::cerr << "File not found: " << zipPath << "\n";
        return 1;
    }

    auto store = StateStore::load();
    auto existing = store.find(modId);

    std::string displayName = existing ? existing->displayName : "mod " + std::to_string(modId);
    std::string fileName = zipPath.filename().string();
    ReleaseChannel channel = existing ? existing->channel : ReleaseChannel::Release;
    std::vector<std::string> gameVersions;
    std::string iconUrl = existing ? existing->iconUrl : std::string();
    std::string slug = existing ? existing->modSlug : std::string();
    std::optional<CurseForgeFile> fetchedFile;

    // Best-effort: enrich with real metadata if a key is configured, but
    // this must work with no API key/network at all (manual pointing is
    // exactly the fallback path for blocked/offline cases).
    try {
        auto cfg = Config::load();
        if (cfg.curseforge_api_key.has_value()) {
            CurseForgeClient client(*cfg.curseforge_api_key);
            auto mod = client.getMod(modId);
            auto file = client.getFile(modId, fileId);
            displayName = mod.name;
            fileName = file.fileName;
            channel = file.releaseType;
            gameVersions = file.gameVersions;
            fetchedFile = file;
            slug = mod.slug;
            if (!mod.logoUrl.empty()) iconUrl = mod.logoUrl;
        }
    } catch (const std::exception& e) {
        std::cerr << "(note: could not fetch metadata, proceeding with local file only: " << e.what() << ")\n";
    }

    auto addonsDir = requireAddonsDir();
    auto folders = AddonInstaller::installZip(zipPath, addonsDir,
                                              existing ? existing->folders : std::vector<std::string>{});

    InstalledAddon rec;
    rec.modId = modId;
    rec.fileId = fileId;
    rec.displayName = displayName;
    rec.fileName = fileName;
    rec.channel = channel;
    rec.gameVersions = gameVersions;
    if (fetchedFile) recordFile(rec, *fetchedFile);
    rec.modSlug = slug;
    rec.folders = folders;
    rec.installedAt = nowIso8601();
    rec.manuallyProvided = true;
    rec.iconUrl = iconUrl;
    if (existing.has_value()) {
        rec.flavorTypeId = existing->flavorTypeId;
        rec.flavorName = existing->flavorName;
    }

    recordAndReport(rec);
    return 0;
}

// Shared by 'update' and 'update-all'. Fetches the current best file for an
// already-tracked mod (within the flavor it was installed for), shows a diff
// against what's installed, and applies it only after confirmation (or
// immediately if assumeYes). Per-mod failures are caught here so update-all
// can keep going past one bad mod.
int updateOneMod(int64_t modId, const std::optional<std::string>& flavor,
                  std::optional<ReleaseChannel> channelOverride, bool assumeYes) {
    auto store = StateStore::load();
    auto existing = store.find(modId);
    if (!existing.has_value()) {
        std::cerr << "Mod " << modId << " is not tracked. Run 'wam install " << modId << "' first.\n";
        return 1;
    }
    ReleaseChannel channel = channelOverride.value_or(existing->channel);

    try {
        auto client = requireClient();
        auto mod = client.getMod(modId);

        // Stored flavor by default; an explicit --flavor overrides and is saved.
        std::optional<int64_t> gvTypeId;
        if (existing->flavorTypeId != 0) gvTypeId = existing->flavorTypeId;
        if (flavor.has_value()) {
            gvTypeId = resolveFlavorOrComplain(client, *flavor, mod.name + ": ");
            if (!gvTypeId.has_value()) {
                std::cerr << mod.name << ": skipping.\n";
                return 1;
            }
        }

        auto files = client.getFiles(modId, gvTypeId);
        auto chosen = CurseForgeClient::selectBestFile(files, channel);
        if (!chosen.has_value()) {
            std::cout << mod.name << ": no " << channelName(channel) << " file found"
                      << (gvTypeId ? " for this flavor" : "") << ". Skipping.\n";
            return 0;
        }

        if (chosen->id == existing->fileId) {
            std::cout << mod.name << ": up to date (" << existing->fileName << ")\n";
            return 0;
        }

        std::cout << mod.name << (existing->flavorName.empty() ? "" : " [" + existing->flavorName + "]") << ":\n"
                  << "  current: " << (existing->fileId == 0 ? "unknown (adopted, never pinned)" : existing->fileName) << "\n"
                  << "  latest:  " << chosen->fileName << " (" << channelName(chosen->releaseType) << ")\n";

        if (chosen->isBlocked()) {
            std::cout << "  Third-party downloads blocked by the author. Download it in your browser from\n"
                       << "    " << CurseForgeClient::browserDownloadUrl(modId, chosen->id) << "\n"
                       << "  then install it manually:\n"
                       << "    wam install-manual " << modId << " " << chosen->id << " <path-to-downloaded-zip>\n";
            return 0;
        }

        if (!assumeYes) {
            std::cout << "  Apply update? [y/N] ";
            std::string answer;
            std::getline(std::cin, answer);
            if (answer != "y" && answer != "Y" && answer != "yes") {
                std::cout << "  Skipped.\n";
                return 0;
            }
        }

        auto addonsDir = requireAddonsDir();
        ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(chosen->id) + "_" + chosen->fileName));
        auto dlResp = HttpClient::downloadToFile(*chosen->downloadUrl, tmpZip.path());
        if (!dlResp.ok()) {
            std::cerr << "  Download failed (HTTP " << dlResp.status << ")\n";
            return 1;
        }

        // Transactional swap: the new file is fully extracted to staging
        // first, then old folders are moved aside and the new ones renamed
        // in. If anything fails, the old version is restored untouched.
        // (SavedVariables live under WTF/, untouched by this.)
        auto folders = AddonInstaller::installZip(tmpZip.path(), addonsDir, existing->folders);

        InstalledAddon rec = *existing;
        recordFile(rec, *chosen);
        rec.modSlug = mod.slug;
        rec.adopted = false; // wam downloaded and installed these files now
        rec.folders = folders;
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
        if (flavor.has_value()) {
            rec.flavorTypeId = *gvTypeId;
            rec.flavorName = flavorNameFor(client, *gvTypeId);
        }

        store.upsert(rec);
        store.save();
        std::cout << "  Updated.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "  Error updating mod " << modId << ": " << e.what() << "\n";
        return 1;
    }
}

int cmdUpdate(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);
    std::optional<std::string> flavor;
    std::optional<ReleaseChannel> channel;
    bool assumeYes = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--flavor" && i + 1 < argc) flavor = argv[++i];
        else if (a == "--channel" && i + 1 < argc) channel = parseChannel(argv[++i]);
        else if (a == "--yes" || a == "-y") assumeYes = true;
    }
    return updateOneMod(modId, flavor, channel, assumeYes);
}

int cmdUpdateAll(int argc, char** argv) {
    std::optional<std::string> flavor;
    bool assumeYes = false;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--flavor" && i + 1 < argc) flavor = argv[++i];
        else if (a == "--yes" || a == "-y") assumeYes = true;
    }

    auto store = StateStore::load();
    int failures = 0;
    for (const auto& addon : store.all()) {
        if (addon.modId == 0) continue; // not CurseForge-sourced (reserved for future sources)
        if (updateOneMod(addon.modId, flavor, std::nullopt, assumeYes) != 0) ++failures;
        std::cout << "\n";
    }
    return failures == 0 ? 0 : 1;
}

int cmdList(int, char**) {
    auto store = StateStore::load();
    std::cout << store.all().size() << " addon(s) tracked:\n";
    for (const auto& a : store.all()) {
        std::cout << "  [" << a.modId << "] " << a.displayName
                   << " - " << a.fileName << " (" << channelName(a.channel) << ")"
                   << (a.flavorName.empty() ? "" : " [" + a.flavorName + "]")
                   << (a.manuallyProvided ? " [manual]" : "") << "\n";
        std::cout << "      folders: " << joinFolders(a.folders);
        std::cout << "\n      installed: " << a.installedAt << "\n";
    }
    return 0;
}

int cmdRemove(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);

    auto store = StateStore::load();
    auto found = store.find(modId);
    if (!found.has_value()) {
        std::cerr << "No tracked addon with mod id " << modId << "\n";
        return 1;
    }

    auto addonsDir = requireAddonsDir();
    AddonInstaller::removeFolders(addonsDir, found->folders);
    store.remove(modId);
    store.save();

    std::cout << "Removed " << found->displayName << " (folders: " << joinFolders(found->folders) << ")\n";
    return 0;
}

int cmdUntrack(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);

    auto store = StateStore::load();
    auto found = store.find(modId);
    if (!found.has_value()) {
        std::cerr << "No tracked addon with mod id " << modId << "\n";
        return 1;
    }

    // Deliberately does NOT touch the AddOns folder: the point is to drop
    // wam's own bookkeeping only, e.g. to re-run 'wam scan'/'adopt' from a
    // clean slate without having to delete real files off disk.
    store.remove(modId);
    store.save();

    std::cout << "Untracked " << found->displayName << " (mod " << modId << "). "
                 "Files left in place, folders: " << joinFolders(found->folders) << "\n";
    return 0;
}

int cmdSetFlavor(int argc, char** argv) {
    if (argc < 2) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);

    auto store = StateStore::load();
    auto existing = store.find(modId);
    if (!existing.has_value()) {
        std::cerr << "No tracked addon with mod id " << modId << "\n";
        return 1;
    }

    auto client = requireClient();
    auto id = resolveFlavorOrComplain(client, argv[1]);
    if (!id.has_value()) return 1;

    InstalledAddon rec = *existing;
    rec.flavorTypeId = *id;
    rec.flavorName = flavorNameFor(client, *id);
    store.upsert(rec);
    store.save();
    std::cout << rec.displayName << " is now recorded as flavor: " << rec.flavorName << "\n";
    return 0;
}

int cmdScan(int, char**) {
    auto addonsDir = requireAddonsDir();
    auto store = StateStore::load();
    auto entries = Reconciler::scan(addonsDir, store);

    if (entries.empty()) {
        std::cout << "No untracked folders in " << addonsDir << "\n";
        return 0;
    }

    auto groups = Reconciler::groupByModId(entries);
    std::cout << entries.size() << " untracked folder(s) in " << addonsDir << ":\n\n";
    for (const auto& group : groups) {
        int64_t modId = group.first;
        if (modId != 0) {
            std::cout << "  Mod " << modId << " (from .toc X-Curse-Project-ID), "
                      << group.second.size() << " folder(s):\n";
        } else {
            std::cout << "  No X-Curse-Project-ID found, " << group.second.size() << " folder(s):\n";
        }
        for (const auto& e : group.second) {
            std::cout << "    " << e.folder;
            if (e.toc.title) std::cout << " (" << *e.toc.title << ")";
            if (e.toc.version) std::cout << " v" << *e.toc.version;
            std::cout << "\n";
        }
    }

    std::cout << "\nTo adopt:\n"
                 "  wam adopt --mod-id <id>\n"
                 "  wam adopt --folder <name> --mod-id <id>\n";
    return 0;
}

int cmdAdopt(int argc, char** argv) {
    std::optional<int64_t> modId;
    std::optional<std::string> folder;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--mod-id" && i + 1 < argc) modId = std::stoll(argv[++i]);
        else if (a == "--folder" && i + 1 < argc) folder = argv[++i];
    }
    if (!modId.has_value()) {
        std::cerr << "wam adopt requires --mod-id <id> (see: wam scan)\n";
        return 1;
    }

    auto addonsDir = requireAddonsDir();
    auto store = StateStore::load();
    std::vector<std::string> foldersToAdopt;

    if (folder.has_value()) {
        if (!std::filesystem::exists(addonsDir / *folder)) {
            std::cerr << "No such folder in AddOns: " << *folder << "\n";
            return 1;
        }
        // Refuse to silently move a folder that's already owned by a
        // different tracked addon: ask for an explicit remove first.
        for (const auto& a : store.all()) {
            if (a.modId == *modId) continue;
            if (std::find(a.folders.begin(), a.folders.end(), *folder) != a.folders.end()) {
                std::cerr << "'" << *folder << "' is already tracked under mod " << a.modId
                          << " (" << a.displayName << "). Run 'wam remove " << a.modId
                          << "' first if this is wrong.\n";
                return 1;
            }
        }
        foldersToAdopt.push_back(*folder);
    } else {
        auto entries = Reconciler::scan(addonsDir, store);
        std::unordered_set<std::string> untrackedOnDisk;
        for (const auto& e : entries) {
            untrackedOnDisk.insert(e.folder);
            if (e.toc.curseProjectId == modId) foldersToAdopt.push_back(e.folder);
        }

        // Self-tagged folders only catch a mod's primary module. Real
        // multi-module addons (e.g. a UI suite split into ActionBars/Bags/
        // etc.) commonly leave every other module's .toc untagged, so also
        // cross-check the mod's known files' moduleNames against what's
        // sitting untracked on disk. This is the same folder list the real
        // 'install' flow trusts, just sourced from the API instead of a
        // fresh extraction.
        if (auto cfg = Config::load(); cfg.curseforge_api_key.has_value()) {
            try {
                CurseForgeClient client(*cfg.curseforge_api_key);
                for (const auto& f : client.getFiles(*modId)) {
                    for (const auto& moduleName : f.moduleNames) {
                        if (!untrackedOnDisk.count(moduleName)) continue;
                        if (std::find(foldersToAdopt.begin(), foldersToAdopt.end(), moduleName) != foldersToAdopt.end())
                            continue;
                        foldersToAdopt.push_back(moduleName);
                    }
                }
            } catch (const std::exception&) {
                // Best-effort only: self-tagged folders above still adopt fine offline.
            }
        }

        if (foldersToAdopt.empty()) {
            std::cerr << "No untracked folder's .toc claims mod " << *modId
                      << ", and no sibling modules were found via the API"
                         " (check --mod-id, or configure an API key for cross-checking)."
                         " Use --folder <name> to assign one by hand.\n";
            return 1;
        }
    }

    // Best-effort metadata only; adoption itself must work with no API key
    // or network: it's fundamentally a local-.toc operation, same
    // constraint install-manual already holds to.
    std::string displayName = "mod " + std::to_string(*modId);
    std::string iconUrl;
    try {
        auto cfg = Config::load();
        if (cfg.curseforge_api_key.has_value()) {
            CurseForgeClient client(*cfg.curseforge_api_key);
            auto mod = client.getMod(*modId);
            displayName = mod.name;
            iconUrl = mod.logoUrl;
        }
    } catch (const std::exception& e) {
        std::cerr << "(note: could not fetch mod name, proceeding with a placeholder: " << e.what() << ")\n";
    }

    InstalledAddon rec;
    try {
        rec = Reconciler::adopt(store, addonsDir, *modId, displayName, iconUrl, foldersToAdopt);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    store.save();

    std::cout << "Adopted " << rec.displayName << " (mod " << rec.modId << "):\n";
    for (const auto& f : foldersToAdopt) std::cout << "  " << f << "\n";
    if (rec.fileId == 0) {
        std::cout << "File/version unknown (adopted from .toc, not a download). "
                     "Run 'wam update " << rec.modId << "' to pin it to a real CurseForge file"
                     " (requires an API key), and 'wam set-flavor " << rec.modId
                  << " <flavor>' to record which flavor it is for.\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(); return 1; }
    std::string cmd = argv[1];

    try {
        if (cmd == "config") return cmdConfig(argc - 2, argv + 2);
        if (cmd == "search") return cmdSearch(argc - 2, argv + 2);
        if (cmd == "flavors") return cmdFlavors(argc - 2, argv + 2);
        if (cmd == "files") return cmdFiles(argc - 2, argv + 2);
        if (cmd == "install") return cmdInstall(argc - 2, argv + 2);
        if (cmd == "update") return cmdUpdate(argc - 2, argv + 2);
        if (cmd == "update-all") return cmdUpdateAll(argc - 2, argv + 2);
        if (cmd == "install-manual") return cmdInstallManual(argc - 2, argv + 2);
        if (cmd == "scan") return cmdScan(argc - 2, argv + 2);
        if (cmd == "adopt") return cmdAdopt(argc - 2, argv + 2);
        if (cmd == "set-flavor") return cmdSetFlavor(argc - 2, argv + 2);
        if (cmd == "list") return cmdList(argc - 2, argv + 2);
        if (cmd == "remove") return cmdRemove(argc - 2, argv + 2);
        if (cmd == "untrack") return cmdUntrack(argc - 2, argv + 2);
        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
