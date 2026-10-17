#include "core/config.hpp"
#include "core/curseforge_client.hpp"
#include "core/addon_installer.hpp"
#include "core/state_store.hpp"
#include "core/http_client.hpp"
#include "core/scoped_temp_file.hpp"
#include "core/toc_reader.hpp"
#include "core/reconciler.hpp"
#include "core/flavor_cache.hpp"
#include "core/profiles.hpp"

#include <iostream>
#include <sstream>
#include <optional>
#include <algorithm>
#include <unordered_set>
#include <set>
#include <cctype>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>

using namespace wam;

namespace {

void printUsage() {
    std::cout <<
        "wam - WoW addon manager core CLI\n\n"
        "Every command acts on the selected profile. Add --profile <name-or-id> to any\n"
        "command to use another profile for that command only.\n\n"
        "Config (the WoW folder and flavor belong to the selected profile):\n"
        "  wam config set-key                (reads the key from stdin, so it stays out of shell history)\n"
        "  wam config set-path <wow_install_dir>   (also works out the folder's flavor)\n"
        "  wam config set-flavor <flavor>\n"
        "  wam config show\n\n"
        "Profiles (named addon sets for one WoW folder; one is active per folder):\n"
        "  wam profile list                  (* = selected, (active) = its addons are in AddOns/)\n"
        "  wam profile create <name>         (empty, same folder and flavor as the selected one)\n"
        "  wam profile select <name-or-id>\n"
        "  wam profile rename <name-or-id> <new-name>\n"
        "  wam profile delete <name-or-id>\n"
        "  wam profile activate [<name-or-id>]     (put its addons in AddOns/; default: selected)\n\n"
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
        "In a profile that is not active, installs, updates and removals change only the\n"
        "profile; AddOns/ changes when it is activated.\n\n"
        "Reconciliation (folders already in AddOns/ that wam didn't put there; active profile only):\n"
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

// Reads one line from stdin, without echo when it is a terminal.
std::string readSecretLine(const char* prompt) {
    const bool tty = ::isatty(STDIN_FILENO);
    termios old{};
    if (tty) {
        std::cerr << prompt << std::flush;
        ::tcgetattr(STDIN_FILENO, &old);
        termios quiet = old;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        ::tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    std::string line;
    std::getline(std::cin, line);
    if (tty) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &old);
        std::cerr << "\n";
    }
    const auto first = line.find_first_not_of(" \t\r\n");
    const auto last = line.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : line.substr(first, last - first + 1);
}

// The profiles, loaded once in main(), and the one this command acts on:
// the selected profile, or the one named by --profile.
std::optional<ProfileStore> gProfiles;
int64_t gProfileId = 0;

ProfileStore& profiles() { return *gProfiles; }

// Re-found on every call: ProfileStore changes can move its profiles.
const Profile& currentProfile() {
    if (const auto* p = profiles().find(gProfileId)) return *p;
    throw std::runtime_error("profile " + std::to_string(gProfileId) + " no longer exists");
}

std::string quoted(const std::string& name) { return "'" + name + "'"; }

bool isAllDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// A profile by id (a number that matches one) or by exact name.
const Profile& resolveProfile(const std::string& arg) {
    if (isAllDigits(arg)) {
        try {
            if (const auto* p = profiles().find(std::stoll(arg))) return *p;
        } catch (const std::out_of_range&) {}
    }
    const Profile* match = nullptr;
    for (const auto& p : profiles().all()) {
        if (p.name != arg) continue;
        if (match) throw std::runtime_error("more than one profile is called " + quoted(arg) + "; use its id (see: wam profile list)");
        match = &p;
    }
    if (!match) throw std::runtime_error("no profile called " + quoted(arg) + " (see: wam profile list)");
    return *match;
}

// The current profile's installed.json, telling the user if it was damaged
// and moved aside.
StateStore loadState() {
    auto store = StateStore::load(profiles().statePath(gProfileId));
    if (!store.damagedFile().empty())
        std::cerr << "warning: " << profiles().statePath(gProfileId) << " could not be read and was moved to "
                  << store.damagedFile() << "; starting with an empty list (run `wam scan` to find your addons again)\n";
    return store;
}

CurseForgeClient requireClient() {
    auto cfg = Config::load();
    if (!cfg.curseforge_api_key.has_value())
        throw std::runtime_error("no CurseForge API key configured, run: wam config set-key");
    return CurseForgeClient(*cfg.curseforge_api_key);
}

// The current profile's AddOns dir. Interrupted installs and switches were
// already recovered at startup (recoverAtStartup).
std::filesystem::path requireAddonsDir() {
    return currentProfile().addonsDir(); // throws if the profile has no WoW folder
}

// scan and adopt read AddOns/, which only holds the active profile's addons.
bool refuseIfInactive(const char* what) {
    const auto& p = currentProfile();
    if (p.active) return false;
    std::cerr << "Profile " << quoted(p.name) << " is not active, so AddOns/ does not hold its addons; "
              << what << " works on the active profile only.\n";
    if (p.wowPath) {
        if (const auto* a = profiles().activeFor(*p.wowPath))
            std::cerr << "Use --profile " << quoted(a->name) << ", or run `wam profile activate " << p.id << "` first.\n";
        else
            std::cerr << "Run `wam profile activate " << p.id << "` first.\n";
    }
    return true;
}

// Before any command that touches AddOns/: put back folders from an install
// that was killed mid-swap, in every active profile's folder, then finish an
// interrupted profile switch.
void recoverAtStartup() {
    std::set<std::filesystem::path> done;
    for (const auto& p : profiles().all()) {
        if (!p.active || !p.wowPath) continue;
        const auto dir = p.addonsDir().lexically_normal();
        if (!done.insert(dir).second || !std::filesystem::exists(dir)) continue;
        auto restored = AddonInstaller::recoverInterrupted(dir);
        if (restored > 0)
            std::cerr << "(recovered " << restored << " folder(s) in " << dir << " from an interrupted install)\n";
    }
    if (ProfileOps::recoverSwitch(profiles()))
        std::cerr << "(finished an interrupted profile switch)\n";
}

// The flavor for the current profile's folder, from its name, matched
// against the flavor cache (refreshed from CurseForge if empty and a key is
// set). Display names first, then CurseForge's own, as the GUI does.
std::optional<FlavorEntry> detectFolderFlavor(const std::string& folderName) {
    auto cache = FlavorCache::load();
    if (cache.all().empty()) {
        try {
            auto cfg = Config::load();
            if (cfg.curseforge_api_key.has_value()) {
                CurseForgeClient client(*cfg.curseforge_api_key);
                cache.merge(client.listGameVersionTypes());
                cache.save();
            }
        } catch (const std::exception&) {} // offline: nothing to match against
    }
    auto id = CurseForgeClient::matchFlavorForFolder(cache.asTypes(), folderName);
    if (!id.has_value()) id = CurseForgeClient::matchFlavorForFolder(cache.asTypes(true), folderName);
    if (!id.has_value()) return std::nullopt;
    return cache.findById(*id);
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

// After a change to a profile that is not active: AddOns/ did not change.
void printInactiveNote() {
    const auto& p = currentProfile();
    if (p.active) return;
    std::cout << "  Profile " << quoted(p.name) << " is not active: AddOns/ is unchanged until you run "
              << "`wam profile activate " << p.id << "`.\n";
}

// Records the install in state and prints a verification summary.
void recordAndReport(const InstalledAddon& addon) {
    auto store = loadState();
    store.upsert(addon);
    store.save();
    AddonLibrary::collectGarbage(profiles()); // a reinstall can leave the old zip unused

    std::cout << "Installed: " << addon.displayName << " (mod " << addon.modId
              << ", file " << addon.fileId << ")\n"
              << "  Channel: " << channelName(addon.channel) << "\n";
    if (!addon.flavorName.empty()) std::cout << "  Flavor:  " << addon.flavorName << "\n";
    std::cout << "  Folders: " << joinFolders(addon.folders)
              << "\n  Source:  " << (addon.manuallyProvided ? "manual file" : "CurseForge download") << "\n";
    printInactiveNote();
}

int cmdConfig(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    std::string sub = argv[0];
    auto cfg = Config::load();

    if (sub == "set-key") {
        if (argc >= 2) {
            // Still accepted, but the key is now in the shell's history file.
            cfg.curseforge_api_key = argv[1];
            std::cerr << "note: a key on the command line is saved in your shell history; "
                         "next time run `wam config set-key` and paste it instead\n";
        } else {
            auto key = readSecretLine("CurseForge API key: ");
            if (key.empty()) { std::cerr << "no key given, nothing saved\n"; return 1; }
            cfg.curseforge_api_key = key;
        }
        cfg.save();
        std::cout << "CurseForge API key saved to " << Config::configPath() << "\n";
        return 0;
    }
    if (sub == "set-path" && argc >= 2) {
        profiles().setWowPath(gProfileId, argv[1]);
        const auto& p = currentProfile();
        std::cout << "Profile " << quoted(p.name) << ": WoW folder set to " << *p.wowPath << "\n";
        std::cout << "AddOns folder resolves to: " << p.addonsDir() << "\n";
        if (!p.flavorId.has_value()) {
            if (auto f = detectFolderFlavor(p.wowFolderName())) {
                profiles().setFlavor(gProfileId, f->id, f->name);
                std::cout << "Flavor: " << f->name << "\n";
            } else {
                std::cout << "Flavor: unknown for folder '" << p.wowFolderName()
                          << "'; set it with: wam config set-flavor <flavor>\n";
            }
        }
        const auto& now = currentProfile();
        if (!now.active) {
            if (const auto* a = profiles().activeFor(*now.wowPath))
                std::cout << "Profile " << quoted(a->name) << " is active on this folder; run `wam profile activate "
                          << now.id << "` to switch to " << quoted(now.name) << ".\n";
            else
                std::cout << "This profile is not active on this folder yet; run `wam profile activate "
                          << now.id << "` to install its addons there.\n";
        }
        return 0;
    }
    if (sub == "set-flavor" && argc >= 2) {
        const std::string want = argv[1];
        auto cache = FlavorCache::load();
        auto id = CurseForgeClient::matchFlavor(cache.asTypes(), want);
        if (!id.has_value()) id = CurseForgeClient::matchFlavor(cache.asTypes(true), want);
        std::string name;
        if (id.has_value()) {
            name = cache.nameFor(*id);
        } else {
            auto client = requireClient();
            id = resolveFlavorOrComplain(client, want);
            if (!id.has_value()) return 1;
            name = flavorNameFor(client, *id);
        }
        profiles().setFlavor(gProfileId, *id, name);
        std::cout << "Profile " << quoted(currentProfile().name) << ": flavor set to " << name << "\n";
        return 0;
    }
    if (sub == "show") {
        const auto& p = currentProfile();
        std::cout << "Config file: " << Config::configPath() << "\n";
        std::cout << "  curseforge_api_key: " << (cfg.curseforge_api_key ? "(set)" : "(not set)") << "\n";
        std::cout << "Profile: " << p.name << " (id " << p.id << ")" << (p.active ? " (active)" : "") << "\n";
        std::cout << "  wow_path: " << (p.wowPath ? *p.wowPath : "(not set)") << "\n";
        std::cout << "  flavor: " << (p.flavorName && !p.flavorName->empty() ? *p.flavorName : "(unknown)") << "\n";
        if (p.wowPath) std::cout << "  AddOns: " << p.addonsDir() << "\n";
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
    std::optional<int64_t> gvTypeId = currentProfile().flavorId; // the profile's flavor by default
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

    // The profile's flavor by default; --flavor overrides it.
    const auto& profile = currentProfile();
    std::optional<int64_t> gvTypeId = profile.flavorId;
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

    requireAddonsDir(); // fail before downloading if the profile has no folder
    ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(chosen->id) + "_" + chosen->fileName));
    auto dlResp = HttpClient::downloadToFile(*chosen->downloadUrl, tmpZip.path());
    if (!dlResp.ok()) {
        std::cerr << "Download failed (HTTP " << dlResp.status << ")\n";
        return 1;
    }

    auto store = loadState();
    auto existing = store.find(modId);

    InstalledAddon rec;
    rec.modId = modId;
    rec.displayName = mod.name;
    recordFile(rec, *chosen);
    rec.modSlug = mod.slug;
    rec.author = mod.author;
    rec.installedAt = nowIso8601();
    rec.manuallyProvided = false;
    rec.iconUrl = !mod.logoUrl.empty() ? mod.logoUrl : (existing ? existing->iconUrl : std::string());
    if (flavor.has_value()) {
        rec.flavorTypeId = *gvTypeId;
        rec.flavorName = flavorNameFor(client, *gvTypeId);
    } else if (gvTypeId.has_value()) {
        rec.flavorTypeId = *gvTypeId;
        rec.flavorName = profile.flavorName.value_or(std::string());
    } else if (existing.has_value()) {
        rec.flavorTypeId = existing->flavorTypeId;
        rec.flavorName = existing->flavorName;
    }

    // Into the library, and into AddOns/ if the profile is active. A
    // reinstall of a tracked mod replaces its old folders atomically.
    rec.folders = ProfileOps::placeZip(profile, rec, tmpZip.path(),
                                       existing ? existing->folders : std::vector<std::string>{});
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

    auto store = loadState();
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

    const auto& profile = currentProfile();
    requireAddonsDir(); // a profile with no folder has nowhere to install to

    InstalledAddon rec;
    rec.modId = modId;
    rec.fileId = fileId;
    rec.displayName = displayName;
    rec.fileName = fileName;
    rec.channel = channel;
    rec.gameVersions = gameVersions;
    if (fetchedFile) recordFile(rec, *fetchedFile);
    rec.modSlug = slug;
    rec.installedAt = nowIso8601();
    rec.manuallyProvided = true;
    rec.iconUrl = iconUrl;
    if (existing.has_value()) {
        rec.flavorTypeId = existing->flavorTypeId;
        rec.flavorName = existing->flavorName;
    }

    rec.folders = ProfileOps::placeZip(profile, rec, zipPath,
                                       existing ? existing->folders : std::vector<std::string>{});
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
    auto store = loadState();
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
        // An addon with no recorded flavor (adopted) uses the profile's.
        std::optional<int64_t> gvTypeId = currentProfile().flavorId;
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

        const auto& profile = currentProfile();
        requireAddonsDir(); // fail before downloading if the profile has no folder
        ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(chosen->id) + "_" + chosen->fileName));
        auto dlResp = HttpClient::downloadToFile(*chosen->downloadUrl, tmpZip.path());
        if (!dlResp.ok()) {
            std::cerr << "  Download failed (HTTP " << dlResp.status << ")\n";
            return 1;
        }

        InstalledAddon rec = *existing;
        recordFile(rec, *chosen);
        rec.modSlug = mod.slug;
        if (!mod.author.empty()) rec.author = mod.author;
        rec.adopted = false; // wam downloaded and installed these files now
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
        if (flavor.has_value()) {
            rec.flavorTypeId = *gvTypeId;
            rec.flavorName = flavorNameFor(client, *gvTypeId);
        }

        // Into the library, and, if the profile is active, a transactional
        // swap in AddOns/: staged first, old folders restored on any failure.
        // (SavedVariables live under WTF/, untouched by this.)
        rec.folders = ProfileOps::placeZip(profile, rec, tmpZip.path(), existing->folders);

        store.upsert(rec);
        store.save();
        AddonLibrary::collectGarbage(profiles()); // the previous version's zip
        std::cout << (profile.active ? "  Updated.\n" : "  Updated in this profile (not active: AddOns/ unchanged).\n");
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

    auto store = loadState();
    int failures = 0;
    for (const auto& addon : store.all()) {
        if (addon.modId == 0) continue; // not CurseForge-sourced (reserved for future sources)
        if (updateOneMod(addon.modId, flavor, std::nullopt, assumeYes) != 0) ++failures;
        std::cout << "\n";
    }
    return failures == 0 ? 0 : 1;
}

int cmdList(int, char**) {
    auto store = loadState();
    const auto& p = currentProfile();
    std::cout << "Profile " << quoted(p.name) << (p.active ? " (active)" : " (not active)") << ": "
              << store.all().size() << " addon(s) tracked:\n";
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

    auto store = loadState();
    auto found = store.find(modId);
    if (!found.has_value()) {
        std::cerr << "No tracked addon with mod id " << modId << "\n";
        return 1;
    }

    // Deletes its folders only if the profile is active.
    ProfileOps::removeAddon(currentProfile(), *found);
    store.remove(modId);
    store.save();
    AddonLibrary::collectGarbage(profiles());

    std::cout << "Removed " << found->displayName << " (folders: " << joinFolders(found->folders) << ")\n";
    printInactiveNote();
    return 0;
}

int cmdUntrack(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);

    auto store = loadState();
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
    AddonLibrary::collectGarbage(profiles());

    std::cout << "Untracked " << found->displayName << " (mod " << modId << "). "
                 "Files left in place, folders: " << joinFolders(found->folders) << "\n";
    return 0;
}

int cmdSetFlavor(int argc, char** argv) {
    if (argc < 2) { printUsage(); return 1; }
    int64_t modId = std::stoll(argv[0]);

    auto store = loadState();
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
    if (refuseIfInactive("scan")) return 1;
    auto addonsDir = requireAddonsDir();
    auto store = loadState();
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
    if (refuseIfInactive("adopt")) return 1;

    auto addonsDir = requireAddonsDir();
    auto store = loadState();
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

// Re-downloads a library zip that is missing, for activate: the file by its
// id from CurseForge, to a temp name beside dest, renamed in when complete.
// False with no API key, or for a blocked file (the user installs it again
// with install-manual).
bool fetchForActivate(const InstalledAddon& addon, const std::filesystem::path& dest) {
    auto cfg = Config::load();
    if (!cfg.curseforge_api_key.has_value() || addon.fileId == 0) return false;
    CurseForgeClient client(*cfg.curseforge_api_key);
    auto file = client.getFile(addon.modId, addon.fileId);
    if (file.isBlocked()) return false;
    std::filesystem::path part = dest;
    part += ".part";
    auto resp = HttpClient::downloadToFile(*file.downloadUrl, part);
    if (!resp.ok()) {
        std::error_code ec;
        std::filesystem::remove(part, ec);
        return false;
    }
    std::filesystem::rename(part, dest);
    return true;
}

// Names must stay usable as arguments: not empty, and unique.
bool checkNewName(const std::string& name, int64_t exceptId = 0) {
    if (name.empty()) {
        std::cerr << "A profile name cannot be empty.\n";
        return false;
    }
    for (const auto& p : profiles().all()) {
        if (p.id != exceptId && p.name == name) {
            std::cerr << "There is already a profile called " << quoted(name) << " (id " << p.id << ").\n";
            return false;
        }
    }
    return true;
}

int cmdProfile(int argc, char** argv) {
    if (argc < 1) { printUsage(); return 1; }
    const std::string sub = argv[0];

    if (sub == "list") {
        for (const auto& p : profiles().all()) {
            const size_t count = StateStore::peek(profiles().statePath(p.id)).value_or(std::vector<InstalledAddon>{}).size();
            std::cout << (p.id == profiles().selectedId() ? "* " : "  ") << "[" << p.id << "] " << p.name
                      << (p.active ? " (active)" : "") << "\n"
                      << "      folder: " << (p.wowPath ? *p.wowPath : "(not set)")
                      << (p.flavorName && !p.flavorName->empty() ? " [" + *p.flavorName + "]" : "")
                      << ", " << count << " addon(s)\n";
        }
        return 0;
    }
    if (sub == "create" && argc >= 2) {
        const std::string name = argv[1];
        if (!checkNewName(name)) return 1;
        const int64_t id = profiles().create(name);
        const auto* p = profiles().find(id);
        std::cout << "Created profile " << quoted(name) << " (id " << id << ") for "
                  << (p->wowPath ? *p->wowPath : std::string("no WoW folder yet")) << ". It has no addons and is not active.\n"
                  << "Use it with --profile " << id << ", or: wam profile select " << id << "\n";
        return 0;
    }
    if (sub == "select" && argc >= 2) {
        const auto& p = resolveProfile(argv[1]);
        profiles().select(p.id);
        std::cout << "Selected profile " << quoted(p.name) << " (id " << p.id << ")"
                  << (p.active ? " (active)" : "") << "\n";
        return 0;
    }
    if (sub == "rename" && argc >= 3) {
        const int64_t id = resolveProfile(argv[1]).id;
        if (!checkNewName(argv[2], id)) return 1;
        const std::string old = profiles().find(id)->name;
        profiles().rename(id, argv[2]);
        std::cout << "Renamed profile " << quoted(old) << " to " << quoted(argv[2]) << "\n";
        return 0;
    }
    if (sub == "delete" && argc >= 2) {
        const auto& p = resolveProfile(argv[1]);
        const std::string name = p.name;
        const int64_t id = p.id;
        profiles().remove(id); // refuses the active profile and the last one
        std::cout << "Deleted profile " << quoted(name) << " (id " << id << ")\n";
        return 0;
    }
    if (sub == "activate") {
        const int64_t id = argc >= 2 ? resolveProfile(argv[1]).id : gProfileId;
        const auto* target = profiles().find(id);
        if (target->active) {
            std::cout << "Profile " << quoted(target->name) << " is already active.\n";
            return 0;
        }
        if (!target->wowPath) {
            std::cerr << "Profile " << quoted(target->name) << " has no WoW folder; set one with: wam --profile "
                      << id << " config set-path <wow_install_dir>\n";
            return 1;
        }
        const std::string name = target->name;
        const std::string folder = *target->wowPath;
        const auto* old = profiles().activeFor(folder);
        std::cout << "Switching " << folder << (old ? " from " + quoted(old->name) : std::string()) << " to "
                  << quoted(name) << "…\n";
        ProfileOps::activate(profiles(), id, fetchForActivate);
        std::cout << "Profile " << quoted(name) << " is now active: AddOns/ holds its addons.\n";
        return 0;
    }
    printUsage();
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    // --profile <name-or-id> may appear anywhere; it is taken out before the
    // command sees its arguments.
    std::vector<char*> args;
    std::optional<std::string> profileArg;
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && std::string(argv[i]) == "--profile") {
            if (i + 1 >= argc) { std::cerr << "--profile needs a profile name or id\n"; return 1; }
            profileArg = argv[++i];
            continue;
        }
        args.push_back(argv[i]);
    }
    argc = static_cast<int>(args.size());
    argv = args.data();

    if (argc < 2) { printUsage(); return 1; }
    std::string cmd = argv[1];

    // Commands that read or change AddOns/, which first recover from an
    // interrupted install or profile switch.
    static const std::set<std::string> touchesAddons = {
        "install", "update", "update-all", "install-manual", "scan", "adopt", "remove", "profile"};

    try {
        gProfiles = ProfileStore::load();
        gProfileId = profileArg ? resolveProfile(*profileArg).id : profiles().selectedId();
        if (touchesAddons.count(cmd)) recoverAtStartup();

        if (cmd == "config") return cmdConfig(argc - 2, argv + 2);
        if (cmd == "profile") return cmdProfile(argc - 2, argv + 2);
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
