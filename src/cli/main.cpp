#include "core/config.hpp"
#include "core/curseforge_client.hpp"
#include "core/addon_installer.hpp"
#include "core/state_store.hpp"
#include "core/http_client.hpp"
#include "core/toc_reader.hpp"
#include "core/reconciler.hpp"

#include <iostream>
#include <sstream>
#include <optional>
#include <algorithm>
#include <cstdlib>

using namespace wam;

namespace {

void printUsage() {
    std::cout <<
        "wam - WoW addon manager core CLI (milestone 1: CurseForge + manual install, no UI)\n\n"
        "Config:\n"
        "  wam config set-key <api_key>\n"
        "  wam config set-path <wow_install_dir>\n"
        "  wam config show\n\n"
        "CurseForge:\n"
        "  wam search <query>\n"
        "  wam flavors                       (list known WoW flavors/version-types)\n"
        "  wam files <modId> [--flavor <name>]\n"
        "  wam install <modId> [--channel release|beta|alpha] [--flavor <name>]\n\n"
        "Manual install (for addons with third-party downloads blocked):\n"
        "  wam install-manual <modId> <fileId> <local-zip-path>\n\n"
        "Reconciliation (folders already in AddOns/ that wam didn't put there):\n"
        "  wam scan                                        (report untracked folders + .toc-detected mod ids)\n"
        "  wam adopt --mod-id <id>                         (adopt all untracked folders tagged for that mod)\n"
        "  wam adopt --folder <name> --mod-id <id>         (manually assign one folder, e.g. no .toc tag found)\n\n"
        "Local state:\n"
        "  wam list\n"
        "  wam remove <modId>\n";
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
        throw std::runtime_error("no CurseForge API key configured — run: wam config set-key <key>");
    return CurseForgeClient(*cfg.curseforge_api_key);
}

std::filesystem::path requireAddonsDir() {
    auto cfg = Config::load();
    return cfg.addonsDir(); // throws if wow_path unset
}

// Records the install in state and prints a verification summary.
void recordAndReport(const InstalledAddon& addon) {
    auto store = StateStore::load();
    store.upsert(addon);
    store.save();

    std::cout << "Installed: " << addon.displayName << " (mod " << addon.modId
              << ", file " << addon.fileId << ")\n"
              << "  Channel: " << channelName(addon.channel) << "\n"
              << "  Folders: ";
    for (size_t i = 0; i < addon.folders.size(); ++i) {
        std::cout << addon.folders[i] << (i + 1 < addon.folders.size() ? ", " : "");
    }
    std::cout << "\n  Source:  " << (addon.manuallyProvided ? "manual file" : "CurseForge download") << "\n";
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
        gvTypeId = client.gameVersionTypeId(*flavor);
        if (!gvTypeId.has_value()) {
            std::cerr << "Unknown flavor '" << *flavor << "'. Known flavors:\n";
            for (const auto& t : client.listGameVersionTypes()) std::cerr << "  " << t.name << "\n";
            return 1;
        }
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
        gvTypeId = client.gameVersionTypeId(*flavor);
        if (!gvTypeId.has_value()) {
            std::cerr << "Unknown flavor '" << *flavor << "'. Known flavors:\n";
            for (const auto& t : client.listGameVersionTypes()) std::cerr << "  " << t.name << "\n";
            return 1;
        }
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
                   << "Download it manually from https://www.curseforge.com/wow/addons/" << mod.slug
                   << " and then run:\n"
                   << "  wam install-manual " << modId << " " << chosen->id << " <path-to-downloaded-zip>\n";
        return 2;
    }

    auto addonsDir = requireAddonsDir();
    auto tmpZip = Config::dataDir() / "tmp" / (std::to_string(chosen->id) + "_" + chosen->fileName);
    auto dlResp = HttpClient::downloadToFile(*chosen->downloadUrl, tmpZip);
    if (!dlResp.ok()) {
        std::cerr << "Download failed (HTTP " << dlResp.status << ")\n";
        return 1;
    }

    auto folders = AddonInstaller::extractZip(tmpZip, addonsDir);
    std::filesystem::remove(tmpZip);

    InstalledAddon rec;
    rec.modId = modId;
    rec.fileId = chosen->id;
    rec.displayName = mod.name;
    rec.fileName = chosen->fileName;
    rec.channel = chosen->releaseType;
    rec.gameVersions = chosen->gameVersions;
    rec.folders = folders;
    rec.installedAt = nowIso8601();
    rec.manuallyProvided = false;

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

    std::string displayName = "mod " + std::to_string(modId);
    std::string fileName = zipPath.filename().string();
    ReleaseChannel channel = ReleaseChannel::Release;
    std::vector<std::string> gameVersions;

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
        }
    } catch (const std::exception& e) {
        std::cerr << "(note: could not fetch metadata, proceeding with local file only: " << e.what() << ")\n";
    }

    auto addonsDir = requireAddonsDir();
    auto folders = AddonInstaller::extractZip(zipPath, addonsDir);

    InstalledAddon rec;
    rec.modId = modId;
    rec.fileId = fileId;
    rec.displayName = displayName;
    rec.fileName = fileName;
    rec.channel = channel;
    rec.gameVersions = gameVersions;
    rec.folders = folders;
    rec.installedAt = nowIso8601();
    rec.manuallyProvided = true;

    recordAndReport(rec);
    return 0;
}

int cmdList(int, char**) {
    auto store = StateStore::load();
    std::cout << store.all().size() << " addon(s) tracked:\n";
    for (const auto& a : store.all()) {
        std::cout << "  [" << a.modId << "] " << a.displayName
                   << " — " << a.fileName << " (" << channelName(a.channel) << ")"
                   << (a.manuallyProvided ? " [manual]" : "") << "\n";
        std::cout << "      folders: ";
        for (size_t i = 0; i < a.folders.size(); ++i)
            std::cout << a.folders[i] << (i + 1 < a.folders.size() ? ", " : "");
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

    std::cout << "Removed " << found->displayName << " (folders: ";
    for (size_t i = 0; i < found->folders.size(); ++i)
        std::cout << found->folders[i] << (i + 1 < found->folders.size() ? ", " : "");
    std::cout << ")\n";
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
            std::cout << "  Mod " << modId << " (from .toc X-Curse-Project-ID) — "
                      << group.second.size() << " folder(s):\n";
        } else {
            std::cout << "  No X-Curse-Project-ID found — " << group.second.size() << " folder(s):\n";
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
        // different tracked addon — ask for an explicit remove first.
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
        for (const auto& e : entries)
            if (e.toc.curseProjectId == modId) foldersToAdopt.push_back(e.folder);

        if (foldersToAdopt.empty()) {
            std::cerr << "No untracked folder's .toc claims mod " << *modId
                      << ". Use --folder <name> to assign one by hand.\n";
            return 1;
        }
    }

    // Best-effort metadata only; adoption itself must work with no API key
    // or network — it's fundamentally a local-.toc operation, same
    // constraint install-manual already holds to.
    std::string displayName = "mod " + std::to_string(*modId);
    try {
        auto cfg = Config::load();
        if (cfg.curseforge_api_key.has_value()) {
            CurseForgeClient client(*cfg.curseforge_api_key);
            displayName = client.getMod(*modId).name;
        }
    } catch (const std::exception& e) {
        std::cerr << "(note: could not fetch mod name, proceeding with a placeholder: " << e.what() << ")\n";
    }

    auto existing = store.find(*modId);
    InstalledAddon rec = existing.value_or(InstalledAddon{});
    if (!existing.has_value()) {
        rec.modId = *modId;
        rec.fileId = 0; // unknown version — this came from a .toc tag, not a download
        rec.displayName = displayName;
        rec.channel = ReleaseChannel::Release;
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
    }
    for (const auto& f : foldersToAdopt)
        if (std::find(rec.folders.begin(), rec.folders.end(), f) == rec.folders.end())
            rec.folders.push_back(f);

    store.upsert(rec);
    store.save();

    std::cout << "Adopted " << rec.displayName << " (mod " << rec.modId << "):\n";
    for (const auto& f : foldersToAdopt) std::cout << "  " << f << "\n";
    if (rec.fileId == 0) {
        std::cout << "File/version unknown (adopted from .toc, not a download) — "
                     "run 'wam files " << rec.modId << "' to see what's current, "
                     "or a future update command will offer to pin it.\n";
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
        if (cmd == "install-manual") return cmdInstallManual(argc - 2, argv + 2);
        if (cmd == "scan") return cmdScan(argc - 2, argv + 2);
        if (cmd == "adopt") return cmdAdopt(argc - 2, argv + 2);
        if (cmd == "list") return cmdList(argc - 2, argv + 2);
        if (cmd == "remove") return cmdRemove(argc - 2, argv + 2);
        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
