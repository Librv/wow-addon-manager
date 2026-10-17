#include "core/profiles.hpp"
#include "core/addon_installer.hpp"
#include "core/atomic_file.hpp"
#include "core/config.hpp"
#include <nlohmann/json.hpp>
#include <zip.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace wam {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr fs::perms kStateFilePerms = fs::perms::owner_read | fs::perms::owner_write |
                                      fs::perms::group_read | fs::perms::others_read;

fs::path profilesFile() { return Config::dataDir() / "profiles.json"; }
fs::path profilesDir() { return Config::dataDir() / "profiles"; }
fs::path switchFile() { return profilesDir() / "switch.json"; }
// Untracked folders a switch overwrites are kept here, one zip per folder,
// until the switch commits or is rolled back.
fs::path switchBackupDir() { return profilesDir() / "switch-backup"; }

void clearSwitchBackup() {
    std::error_code ec;
    fs::remove_all(switchBackupDir(), ec);
}

// The form paths are compared in: "/a/_retail_/" and "/a/./_retail_" are
// the same folder.
std::string normalPath(const std::string& p) {
    auto n = fs::path(p).lexically_normal();
    if (n.filename().empty() && n.has_parent_path() && n != n.root_path()) n = n.parent_path();
    return n.string();
}

// A symlinked spelling of a folder is the same folder. weakly_canonical
// also works for folders that do not exist yet; lexical if it fails.
std::string canonicalPath(const std::string& p) {
    std::error_code ec;
    const auto c = fs::weakly_canonical(fs::path(p), ec);
    return normalPath(ec ? p : c.string());
}

bool samePath(const std::optional<std::string>& a, const std::string& b) {
    return a.has_value() && canonicalPath(*a) == canonicalPath(b);
}

// Same naming as StateStore's damaged files: no colons, easy to handle.
fs::path setAside(const fs::path& file) {
    auto stamp = nowIso8601();
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    fs::path aside = file;
    aside += ".damaged-" + stamp;
    std::error_code ec;
    fs::rename(file, aside, ec);
    return aside;
}

json toJson(const Profile& p) {
    json j{{"id", p.id}, {"name", p.name}, {"autoNamed", p.autoNamed}, {"active", p.active}};
    j["wowPath"] = p.wowPath ? json(*p.wowPath) : json(nullptr);
    j["flavorId"] = p.flavorId ? json(*p.flavorId) : json(nullptr);
    j["flavorName"] = p.flavorName ? json(*p.flavorName) : json(nullptr);
    return j;
}

template <typename T>
std::optional<T> optionalField(const json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    return j.at(key).get<T>();
}

Profile fromJson(const json& j) {
    Profile p;
    p.id = j.at("id").get<int64_t>(); // the one field that cannot default
    p.name = j.value("name", "");
    p.autoNamed = j.value("autoNamed", false);
    p.wowPath = optionalField<std::string>(j, "wowPath");
    p.flavorId = optionalField<int64_t>(j, "flavorId");
    p.flavorName = optionalField<std::string>(j, "flavorName");
    p.active = j.value("active", false);
    return p;
}

std::string addonLabel(const InstalledAddon& a) {
    return a.displayName.empty() ? "mod " + std::to_string(a.modId) : a.displayName;
}

// Folders on disk that would go into a snapshot. A folder deleted by hand is
// skipped: there is nothing left to keep.
std::vector<std::string> existingFolders(const fs::path& addonsDir, const std::vector<std::string>& folders) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& f : folders)
        if (AddonInstaller::isSafeFolderName(f) && fs::is_directory(addonsDir / f, ec)) out.push_back(f);
    return out;
}

// One install step of a switch plan.
struct PlannedInstall {
    int64_t modId = 0;
    fs::path zip;
    std::vector<std::string> replace;
    std::vector<std::string> folders; // what the zip puts in AddOns/
    bool preexisting = false; // snapshotted from folders already in AddOns/: rollback keeps them
};

// Thrown by crashSwitchAfterInstallsForTesting. Not a std::exception, so
// nothing rolls back, as when the process dies.
struct SimulatedCrash {};

void savePlan(const json& plan) {
    fs::create_directories(switchFile().parent_path());
    writeFileAtomic(switchFile(), plan.dump(2) + "\n", kStateFilePerms);
}

// The plan of an unfinished switch, or nullopt if there is none or it
// cannot be read.
std::optional<json> readPlan() {
    std::error_code ec;
    if (!fs::exists(switchFile(), ec)) return std::nullopt;
    try {
        std::ifstream in(switchFile());
        json plan;
        in >> plan;
        plan.at("to").get<int64_t>();
        return plan;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<InstalledAddon> peekOrThrow(const ProfileStore& profiles, int64_t id) {
    auto addons = StateStore::peek(profiles.statePath(id));
    if (!addons) {
        const auto* p = profiles.find(id);
        throw std::runtime_error("the addon list of profile '" + (p ? p->name : std::to_string(id)) +
                                 "' cannot be read (" + profiles.statePath(id).string() + ")");
    }
    return *addons;
}

// listZipFolders only reads the zip's index. This reads every entry to the
// end, so corrupt data (a CRC mismatch) fails here and not halfway through
// a switch.
void verifyZip(const fs::path& zip) {
    AddonInstaller::listZipFolders(zip);
    int errCode = 0;
    zip_t* archive = zip_open(zip.string().c_str(), ZIP_RDONLY | ZIP_CHECKCONS, &errCode);
    if (!archive) throw std::runtime_error("cannot open " + zip.string());
    const zip_int64_t n = zip_get_num_entries(archive, 0);
    char buf[65536];
    for (zip_int64_t i = 0; i < n; ++i) {
        zip_file_t* zf = zip_fopen_index(archive, i, 0);
        if (!zf) {
            zip_close(archive);
            throw std::runtime_error("cannot read an entry of " + zip.string());
        }
        zip_int64_t got;
        while ((got = zip_fread(zf, buf, sizeof(buf))) > 0) {}
        zip_fclose(zf);
        if (got < 0) {
            zip_close(archive);
            throw std::runtime_error("corrupt data in " + zip.string());
        }
    }
    zip_close(archive);
}

// Undoes a failed step 4, touching only what the plan got to. The plan
// records its progress: `removeBegun` and `started` (how many installs were
// begun). Folders of steps that never started, and old addons the switch never
// touched, are left exactly as they are, local edits included.
void rollbackPlan(const ProfileStore& profiles, const json& plan) {
    const fs::path addonsDir = plan.at("addonsDir").get<std::string>();
    const int64_t fromId = plan.value("from", int64_t{0});
    const auto keptMods = plan.value("keptMods", std::vector<int64_t>{});
    const auto keptList = plan.value("keptFolders", std::vector<std::string>{});
    const std::set<std::string> keptFolders(keptList.begin(), keptList.end());
    const std::vector<InstalledAddon> oldAddons = fromId ? peekOrThrow(profiles, fromId) : std::vector<InstalledAddon>{};
    const auto& steps = plan.at("install");
    const size_t started = std::min<size_t>(plan.value("started", steps.size()), steps.size());
    const bool removeBegun = plan.value("removeBegun", true);

    AddonInstaller::recoverInterrupted(addonsDir);

    std::set<std::string> oldOwned;
    for (const auto& a : oldAddons) oldOwned.insert(a.folders.begin(), a.folders.end());

    // What the plan may have changed in AddOns/.
    std::set<std::string> touched;
    std::set<std::string> installedHere; // new folders from started steps
    std::vector<std::string> remove;
    if (removeBegun)
        for (const auto& f : plan.value("remove", std::vector<std::string>{})) touched.insert(f);
    for (size_t i = 0; i < started; ++i) {
        const auto& step = steps.at(i);
        const auto folders = step.value("folders", std::vector<std::string>{});
        for (const auto& f : step.value("replace", std::vector<std::string>{})) touched.insert(f);
        for (const auto& f : folders) touched.insert(f);
        if (step.value("preexisting", false)) continue; // the profile's own folders: kept
        for (const auto& f : folders) {
            installedHere.insert(f);
            if (!keptFolders.count(f) && !oldOwned.count(f)) remove.push_back(f);
        }
    }
    AddonInstaller::removeFolders(addonsDir, remove);

    // Untracked folders a started step overwrote come back from the backup.
    for (const auto& f : plan.value("untracked", std::vector<std::string>{})) {
        if (!installedHere.count(f)) continue;
        const fs::path zip = switchBackupDir() / (f + ".zip");
        if (!fs::exists(zip)) continue;
        try {
            AddonInstaller::listZipFolders(zip);
        } catch (const std::exception&) {
            fs::create_directories(addonsDir / f); // it was an empty folder
            continue;
        }
        AddonInstaller::installZip(zip, addonsDir, {f});
    }

    // Old addons the switch touched are reinstalled from the library.
    for (const auto& a : oldAddons) {
        if (std::find(keptMods.begin(), keptMods.end(), a.modId) != keptMods.end()) continue;
        const bool wasTouched = std::any_of(a.folders.begin(), a.folders.end(),
                                            [&](const std::string& f) { return touched.count(f) > 0; });
        if (!wasTouched) continue;
        const fs::path zip = AddonLibrary::zipFor(fromId, a);
        if (fs::exists(zip)) AddonInstaller::installZip(zip, addonsDir, a.folders);
    }
}

// Steps 4 and 5 of activate, from the plan in switch.json. Every step is
// safe to repeat: removeFolders skips folders already gone, and installZip
// replaces whatever is at the replace and zip folders. Progress is written
// back to switch.json before each phase, for rollbackPlan.
void runPlan(ProfileStore& profiles, json& plan) {
    const int64_t targetId = plan.at("to").get<int64_t>();
    const fs::path addonsDir = plan.at("addonsDir").get<std::string>();
    const auto remove = plan.at("remove").get<std::vector<std::string>>();

    AddonInstaller::recoverInterrupted(addonsDir);
    plan["removeBegun"] = true;
    savePlan(plan);
    AddonInstaller::removeFolders(addonsDir, remove);

    peekOrThrow(profiles, targetId); // a real load would set a damaged file aside
    auto state = StateStore::load(profiles.statePath(targetId));
    int installed = 0;
    size_t index = 0;
    for (const auto& step : plan.at("install")) {
        if (ProfileOps::crashSwitchAfterInstallsForTesting >= 0 &&
            installed >= ProfileOps::crashSwitchAfterInstallsForTesting)
            throw SimulatedCrash{};
        if (ProfileOps::failSwitchAfterInstallsForTesting >= 0 &&
            installed >= ProfileOps::failSwitchAfterInstallsForTesting)
            throw std::runtime_error("injected failure");
        ++installed;
        ++index;
        if (plan.value("started", size_t{0}) < index) {
            plan["started"] = index;
            savePlan(plan);
        }
        const int64_t modId = step.at("modId").get<int64_t>();
        const auto folders = AddonInstaller::installZip(step.at("zip").get<std::string>(), addonsDir,
                                                        step.at("replace").get<std::vector<std::string>>());
        if (auto a = state.find(modId)) {
            a->folders = folders;
            state.upsert(*a);
        }
    }
    state.save();
}

} // namespace

// ---- Profile ----

fs::path Profile::addonsDir() const {
    if (!wowPath.has_value())
        throw std::runtime_error("profile '" + name + "' has no WoW folder");
    return fs::path(*wowPath) / "Interface" / "AddOns";
}

std::string Profile::wowFolderName() const {
    if (!wowPath.has_value()) return {};
    return fs::path(normalPath(*wowPath)).filename().string();
}

// ---- AddonLibrary ----

fs::path AddonLibrary::dir() { return Config::dataDir() / "library"; }

fs::path AddonLibrary::zipFor(int64_t profileId, const InstalledAddon& addon) {
    const fs::path modDir = dir() / std::to_string(addon.modId);
    if (addon.fileId != 0) return modDir / (std::to_string(addon.fileId) + ".zip");
    return modDir / ("adopted-" + std::to_string(profileId) + ".zip");
}

fs::path AddonLibrary::store(const fs::path& zip, const fs::path& dest, bool replace) {
    if (!replace && fs::exists(dest)) return dest;
    fs::create_directories(dest.parent_path());
    fs::path tmp = dest;
    tmp += ".tmp";
    fs::copy_file(zip, tmp, fs::copy_options::overwrite_existing);
    fs::rename(tmp, dest);
    return dest;
}

void AddonLibrary::snapshot(const fs::path& addonsDir, const std::vector<std::string>& folders,
                            const fs::path& dest) {
    if (folders.empty()) throw std::runtime_error("nothing to snapshot for " + dest.string());
    for (const auto& f : folders) {
        if (!AddonInstaller::isSafeFolderName(f))
            throw std::runtime_error("refusing to snapshot unsafe folder name: '" + f + "'");
        if (!fs::is_directory(addonsDir / f))
            throw std::runtime_error("cannot snapshot missing folder: " + (addonsDir / f).string());
    }

    fs::create_directories(dest.parent_path());
    fs::path tmp = dest;
    tmp += ".tmp";
    std::error_code ec;
    fs::remove(tmp, ec);

    int errCode = 0;
    zip_t* z = zip_open(tmp.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errCode);
    if (!z) throw std::runtime_error("could not create " + tmp.string());
    auto fail = [&](const std::string& why) {
        zip_discard(z);
        fs::remove(tmp, ec);
        throw std::runtime_error("could not snapshot into " + dest.string() + ": " + why);
    };

    for (const auto& f : folders) {
        if (zip_dir_add(z, (f + "/").c_str(), ZIP_FL_ENC_UTF_8) < 0) fail(zip_strerror(z));
        for (auto it = fs::recursive_directory_iterator(addonsDir / f);
             it != fs::recursive_directory_iterator(); ++it) {
            const std::string name = f + "/" + it->path().lexically_relative(addonsDir / f).generic_string();
            // installZip would refuse this entry later, when it is too late
            // to get the folder back: refuse it now instead.
            if (name.find("..") != std::string::npos) fail("unsupported file name '" + name + "'");
            if (it->is_directory()) {
                if (zip_dir_add(z, (name + "/").c_str(), ZIP_FL_ENC_UTF_8) < 0) fail(zip_strerror(z));
            } else if (it->is_regular_file()) {
                // Read lazily at zip_close, while the folder is still in place.
                zip_source_t* src = zip_source_file(z, it->path().c_str(), 0, ZIP_LENGTH_TO_END);
                if (!src) fail(zip_strerror(z));
                if (zip_file_add(z, name.c_str(), src, ZIP_FL_ENC_UTF_8) < 0) {
                    zip_source_free(src);
                    fail(zip_strerror(z));
                }
            }
        }
    }
    if (zip_close(z) != 0) {
        std::string why = zip_strerror(z);
        zip_discard(z);
        fs::remove(tmp, ec);
        throw std::runtime_error("could not snapshot into " + dest.string() + ": " + why);
    }
    fs::rename(tmp, dest);
}

void AddonLibrary::collectGarbage(const ProfileStore& profiles) {
    // Every profiles/<id>/ on disk counts, not only the listed profiles: after
    // a damaged profiles.json the old folders are unlisted but still hold
    // the user's addon lists. remove() deletes a profile's folder first.
    std::set<int64_t> ids;
    for (const auto& p : profiles.all()) ids.insert(p.id);
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(profilesDir(), ec)) {
        const auto name = d.path().filename().string();
        if (d.is_directory(ec) && !name.empty() && name.size() <= 18 && name.find_first_not_of("0123456789") == std::string::npos)
            ids.insert(std::stoll(name));
    }

    std::set<fs::path> referenced;
    for (const auto id : ids) {
        // Read only: a damaged list must stay where it is, and while its
        // zips are unknown none can be called unused.
        const auto addons = StateStore::peek(profiles.statePath(id));
        if (!addons) return;
        for (const auto& a : *addons) referenced.insert(zipFor(id, a).lexically_normal());
    }
    if (fs::exists(switchFile(), ec)) {
        const auto plan = readPlan();
        if (!plan) return; // an unreadable plan may name any zip
        for (const auto& step : plan->at("install"))
            referenced.insert(fs::path(step.at("zip").get<std::string>()).lexically_normal());
    }

    if (!fs::is_directory(dir(), ec)) return;
    for (const auto& modDir : fs::directory_iterator(dir(), ec)) {
        if (!modDir.is_directory(ec)) continue;
        std::vector<fs::path> drop;
        for (const auto& file : fs::directory_iterator(modDir.path(), ec))
            if (file.path().extension() == ".zip" && !referenced.count(file.path().lexically_normal()))
                drop.push_back(file.path());
        for (const auto& f : drop) fs::remove(f, ec);
        if (fs::is_empty(modDir.path(), ec)) fs::remove(modDir.path(), ec);
    }
}

// ---- ProfileStore ----

ProfileStore ProfileStore::load() {
    ProfileStore store;
    store.path_ = profilesFile();

    if (fs::exists(store.path_)) {
        try {
            std::ifstream in(store.path_);
            json j;
            in >> j;
            for (const auto& item : j.at("profiles")) store.profiles_.push_back(fromJson(item));
            store.selected_ = j.value("selected", int64_t{0});
            store.nextId_ = j.value("nextId", int64_t{1});
            if (store.profiles_.empty()) throw std::runtime_error("no profiles");
        } catch (const std::exception&) {
            // Keep the bytes for the user, and start again below.
            setAside(store.path_);
            store.profiles_.clear();
            store.selected_ = 0;
            store.nextId_ = 1;
        }
    }

    if (store.profiles_.empty()) {
        // Profile folders left by a lost or damaged profiles.json are newer
        // than config.json and installed.json, which stop being updated once
        // profiles exist. Keep them, inactive, since which one is in AddOns/
        // is not known.
        std::error_code ec;
        std::vector<int64_t> found;
        for (const auto& d : fs::directory_iterator(profilesDir(), ec)) {
            const auto name = d.path().filename().string();
            if (d.is_directory(ec) && !name.empty() && name.size() <= 18 &&
                name.find_first_not_of("0123456789") == std::string::npos)
                found.push_back(std::stoll(name));
        }
        std::sort(found.begin(), found.end());
        for (const auto id : found) {
            if (id <= 0) continue;
            Profile p;
            p.id = id;
            p.name = "Recovered " + std::to_string(id);
            store.profiles_.push_back(p);
        }
        if (!store.profiles_.empty()) {
            store.selected_ = store.profiles_.front().id;
            store.nextId_ = store.profiles_.back().id + 1;
            store.save();
        }
    }

    if (store.profiles_.empty()) {
        const Config cfg = Config::load();
        Profile p;
        p.id = 1;
        p.wowPath = cfg.wow_path;
        p.flavorId = cfg.wow_flavor_id;
        p.flavorName = cfg.wow_flavor_name;
        p.name = cfg.wow_flavor_name && !cfg.wow_flavor_name->empty() ? *cfg.wow_flavor_name : "Default";
        p.autoNamed = true;
        p.active = cfg.wow_path.has_value();

        const fs::path legacy = Config::dataDir() / "installed.json";
        if (fs::exists(legacy)) {
            fs::create_directories(store.statePath(p.id).parent_path());
            fs::copy_file(legacy, store.statePath(p.id), fs::copy_options::overwrite_existing);
        }
        store.profiles_.push_back(p);
        store.selected_ = p.id;
        store.nextId_ = p.id + 1;
        store.save();
    }

    int64_t maxId = 0;
    for (const auto& p : store.profiles_) maxId = std::max(maxId, p.id);
    store.nextId_ = std::max(store.nextId_, maxId + 1);
    if (!store.find(store.selected_)) store.selected_ = store.profiles_.front().id;
    return store;
}

void ProfileStore::save() const {
    json arr = json::array();
    for (const auto& p : profiles_) arr.push_back(toJson(p));
    json root{{"selected", selected_}, {"nextId", nextId_}, {"profiles", arr}};
    const fs::path path = path_.empty() ? profilesFile() : path_;
    fs::create_directories(path.parent_path());
    writeFileAtomic(path, root.dump(2) + "\n", kStateFilePerms);
}

const Profile* ProfileStore::find(int64_t id) const {
    auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.id == id; });
    return it == profiles_.end() ? nullptr : &*it;
}

Profile* ProfileStore::findMutable(int64_t id) {
    auto* p = const_cast<Profile*>(find(id));
    if (!p) throw std::runtime_error("no profile with id " + std::to_string(id));
    return p;
}

const Profile& ProfileStore::selected() const {
    if (const auto* p = find(selected_)) return *p;
    throw std::runtime_error("no profile is selected");
}

void ProfileStore::select(int64_t id) {
    findMutable(id);
    selected_ = id;
    save();
}

int64_t ProfileStore::create(const std::string& name) {
    Profile p;
    p.id = nextId_++;
    p.name = name;
    if (const auto* sel = find(selected_)) {
        p.wowPath = sel->wowPath;
        p.flavorId = sel->flavorId;
        p.flavorName = sel->flavorName;
    }
    profiles_.push_back(p);
    save();
    return p.id;
}

void ProfileStore::rename(int64_t id, const std::string& name) {
    auto* p = findMutable(id);
    p->name = name;
    p->autoNamed = false;
    save();
}

void ProfileStore::remove(int64_t id) {
    const auto* p = findMutable(id);
    if (profiles_.size() <= 1) throw std::runtime_error("cannot remove the last profile");
    if (p->active)
        throw std::runtime_error("cannot remove '" + p->name + "': its addons are in the WoW folder; activate another profile first");
    if (const auto plan = readPlan();
        plan && (plan->value("to", int64_t{0}) == id || plan->value("from", int64_t{0}) == id))
        throw std::runtime_error("cannot remove '" + p->name + "' while a switch to or from it is unfinished");

    profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(), [&](const Profile& q) { return q.id == id; }),
                    profiles_.end());
    if (selected_ == id) selected_ = profiles_.front().id;
    save(); // before deleting its folder, so a crash never lists a profile with no state
    std::error_code ec;
    fs::remove_all(profilesDir() / std::to_string(id), ec);
    AddonLibrary::collectGarbage(*this);
}

void ProfileStore::setWowPath(int64_t id, const std::string& path) {
    auto* p = findMutable(id);
    if (samePath(p->wowPath, path)) return;
    const bool wasActive = p->active;
    const bool othersThere = activeFor(path) != nullptr; // p is not on `path` yet, so it never counts
    p->wowPath = path;
    p->flavorId.reset();
    p->flavorName.reset();
    if (othersThere) {
        p->active = false;
    } else if (!wasActive) {
        const auto addons = StateStore::peek(statePath(id)); // read only: p may not be the selected one
        p->active = addons && addons->empty();
    }
    save();
}

void ProfileStore::setFlavor(int64_t id, std::optional<int64_t> flavorId, std::optional<std::string> flavorName) {
    auto* p = findMutable(id);
    p->flavorId = flavorId;
    p->flavorName = flavorName;
    if (p->autoNamed && flavorName && !flavorName->empty()) p->name = *flavorName;
    save();
}

const Profile* ProfileStore::activeFor(const std::string& wowPath) const {
    for (const auto& p : profiles_)
        if (p.active && samePath(p.wowPath, wowPath)) return &p;
    return nullptr;
}

fs::path ProfileStore::statePath(int64_t id) const {
    return profilesDir() / std::to_string(id) / "installed.json";
}

// ---- ProfileOps ----

std::vector<std::string> ProfileOps::placeZip(const Profile& profile, const InstalledAddon& addon,
                                              const fs::path& zip, const std::vector<std::string>& replaceFolders) {
    const fs::path dest = AddonLibrary::zipFor(profile.id, addon);
    // Check or install first, so a broken zip never lands in the library.
    if (!profile.active) {
        auto folders = AddonInstaller::listZipFolders(zip);
        // An adopted addon's zip is this profile's own copy: a new one
        // replaces it. A zip keyed by fileId is the same file whoever stored it.
        AddonLibrary::store(zip, dest, addon.fileId == 0);
        return folders;
    }
    auto folders = AddonInstaller::installZip(zip, profile.addonsDir(), replaceFolders);
    try {
        AddonLibrary::store(zip, dest, addon.fileId == 0);
    } catch (const std::exception&) {
        // The addon is installed and must be recorded. Without its zip, the
        // next switch away snapshots it from its folders.
    }
    return folders;
}

void ProfileOps::removeAddon(const Profile& profile, const InstalledAddon& addon) {
    if (profile.active) AddonInstaller::removeFolders(profile.addonsDir(), addon.folders);
}

void ProfileOps::activate(ProfileStore& profiles, int64_t targetId, const FetchFn& fetch) {
    if (fs::exists(switchFile())) recoverSwitch(profiles);

    const Profile* target = profiles.find(targetId);
    if (!target) throw std::runtime_error("no profile with id " + std::to_string(targetId));
    if (target->active) return;
    if (!target->wowPath) throw std::runtime_error("profile '" + target->name + "' has no WoW folder");

    const fs::path addonsDir = target->addonsDir();
    const Profile* old = profiles.activeFor(*target->wowPath);
    const int64_t oldId = old ? old->id : 0;
    // Read only: a list that cannot be read stops the switch, it must not
    // be set aside from under its profile.
    const std::vector<InstalledAddon> oldAddons = old ? peekOrThrow(profiles, oldId) : std::vector<InstalledAddon>{};
    const std::vector<InstalledAddon> targetAddons = peekOrThrow(profiles, targetId);

    // A leftover half swap would make the snapshots below wrong.
    if (fs::exists(addonsDir)) AddonInstaller::recoverInterrupted(addonsDir);

    // Left over from a switch that was dropped; this one starts clean.
    clearSwitchBackup();

    // 1. Keep what the old profile has that the library does not. An adopted
    // addon's snapshot is its own copy of the folder, so it is taken again to
    // include edits made since; a real file's zip never changes.
    for (const auto& a : oldAddons) {
        const fs::path zip = AddonLibrary::zipFor(oldId, a);
        const bool refresh = a.fileId == 0;
        if (fs::exists(zip) && !refresh) continue;
        const auto folders = existingFolders(addonsDir, a.folders);
        if (folders.empty()) continue;
        try {
            AddonLibrary::snapshot(addonsDir, folders, zip); // temp file + rename
        } catch (const std::exception&) {
            if (!fs::exists(zip)) throw; // an older copy is better than none
        }
    }

    // The diff. Same modId and fileId (a real file) stays put.
    std::map<int64_t, const InstalledAddon*> oldByMod;
    for (const auto& a : oldAddons) oldByMod[a.modId] = &a;
    std::set<int64_t> kept;
    std::set<std::string> keptFolders;
    for (const auto& a : targetAddons) {
        auto it = oldByMod.find(a.modId);
        if (it != oldByMod.end() && a.fileId != 0 && it->second->fileId == a.fileId) {
            kept.insert(a.modId);
            keptFolders.insert(it->second->folders.begin(), it->second->folders.end());
        }
    }
    auto notKept = [&](const std::vector<std::string>& folders) {
        std::vector<std::string> out;
        for (const auto& f : folders)
            if (!keptFolders.count(f)) out.push_back(f);
        return out;
    };

    // 2. Every zip the switch installs must be in the library and readable.
    std::vector<PlannedInstall> installs;
    std::vector<std::string> missing;
    for (const auto& a : targetAddons) {
        if (kept.count(a.modId)) continue;
        const fs::path zip = AddonLibrary::zipFor(targetId, a);
        std::string why; // what is wrong with the library zip, for the message
        bool ok = false;
        const bool inLibrary = fs::exists(zip);
        if (inLibrary) {
            try {
                verifyZip(zip); // every entry, so corrupt data fails before AddOns/ changes
                ok = true;
            } catch (const std::exception& e) {
                why = e.what();
            }
        }
        // Missing or unusable: fetch it again, into a side file so a failed
        // fetch leaves whatever was there.
        if (!ok && fetch && a.fileId != 0) {
            const fs::path fetched = zip.parent_path() / (std::to_string(a.fileId) + ".fetching.zip");
            std::error_code ec;
            try {
                fs::create_directories(zip.parent_path());
                fs::remove(fetched, ec);
                if (fetch(a, fetched) && fs::exists(fetched)) {
                    verifyZip(fetched);
                    fs::rename(fetched, zip);
                    ok = true;
                } else if (why.empty()) {
                    why = "it is not in the library and could not be fetched";
                }
            } catch (const std::exception& e) {
                why = e.what();
            }
            fs::remove(fetched, ec);
        }
        bool preexisting = false;
        // Nobody manages this folder (say the profile was pointed elsewhere
        // and back), but the addon's own folders are still in it: keep those.
        if (!ok && !old) {
            const auto folders = existingFolders(addonsDir, a.folders);
            if (!folders.empty() && folders.size() == a.folders.size()) {
                try {
                    AddonLibrary::snapshot(addonsDir, folders, zip);
                    verifyZip(zip);
                    ok = true;
                    preexisting = true;
                } catch (const std::exception&) {
                    ok = false;
                }
            }
        }
        if (!ok && why.empty()) why = "it is not in the library";
        if (!ok && !fetch && inLibrary) why += "; delete it to have it fetched again";
        if (!ok) {
            missing.push_back(addonLabel(a) + " (" + zip.string() + ": " + why + ")");
            continue;
        }
        PlannedInstall step{a.modId, zip, {}, AddonInstaller::listZipFolders(zip), preexisting};
        if (auto it = oldByMod.find(a.modId); it != oldByMod.end()) step.replace = notKept(it->second->folders);
        installs.push_back(std::move(step));
    }
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        throw std::runtime_error("cannot switch to '" + target->name +
                                 "': no usable zip for " + list + ". Nothing in the WoW folder was changed.");
    }

    std::set<int64_t> targetMods;
    for (const auto& a : targetAddons) targetMods.insert(a.modId);
    std::vector<std::string> remove;
    for (const auto& a : oldAddons)
        if (!targetMods.count(a.modId))
            for (const auto& f : notKept(a.folders)) remove.push_back(f);

    // Untracked folders (not owned by any old addon, not kept) that a zip is
    // about to overwrite: keep a copy until the switch is committed.
    std::set<std::string> oldOwned, preexistingFolders;
    for (const auto& a : oldAddons) oldOwned.insert(a.folders.begin(), a.folders.end());
    for (const auto& st : installs)
        if (st.preexisting) preexistingFolders.insert(st.folders.begin(), st.folders.end());
    std::vector<std::string> untracked;
    try {
        std::error_code ec;
        for (const auto& st : installs) {
            if (st.preexisting) continue;
            for (const auto& f : st.folders) {
                if (oldOwned.count(f) || keptFolders.count(f) || preexistingFolders.count(f)) continue;
                if (std::find(untracked.begin(), untracked.end(), f) != untracked.end()) continue;
                if (!fs::is_directory(addonsDir / f, ec)) continue;
                AddonLibrary::snapshot(addonsDir, {f}, switchBackupDir() / (f + ".zip"));
                untracked.push_back(f);
            }
        }

        // 3. The plan, so an interrupted switch can be finished or undone.
        json plan{{"from", oldId}, {"to", targetId}, {"addonsDir", addonsDir.string()},
                  {"wowPath", *target->wowPath}, {"remove", remove}, {"untracked", untracked},
                  {"removeBegun", false}, {"started", 0},
                  {"keptMods", std::vector<int64_t>(kept.begin(), kept.end())},
                  {"keptFolders", std::vector<std::string>(keptFolders.begin(), keptFolders.end())}};
        plan["install"] = json::array();
        for (const auto& st : installs)
            plan["install"].push_back({{"modId", st.modId}, {"zip", st.zip.string()}, {"replace", st.replace},
                                       {"folders", st.folders}, {"preexisting", st.preexisting}});
        savePlan(plan);
    } catch (...) {
        clearSwitchBackup(); // nothing in AddOns/ has changed yet
        throw;
    }

    // 4 and 5. A failure here leaves switch.json for recoverSwitch.
    recoverSwitch(profiles);
}

bool ProfileOps::recoverSwitch(ProfileStore& profiles) {
    const fs::path file = switchFile();
    if (!fs::exists(file)) return false;

    json plan;
    try {
        std::ifstream in(file);
        in >> plan;
        plan.at("to").get<int64_t>();
    } catch (const std::exception&) {
        // A plan that cannot be read cannot be finished. writeFileAtomic
        // makes this unlikely; keep the bytes and leave AddOns/ as it is.
        setAside(file);
        return false;
    }

    const int64_t targetId = plan.at("to").get<int64_t>();
    const int64_t fromId = plan.value("from", int64_t{0});
    const Profile* target = profiles.find(targetId);
    // The folder comes from the plan: the target may be a recovered profile
    // with no folder of its own yet.
    const std::string wowPath = plan.value("wowPath", target && target->wowPath ? *target->wowPath : std::string());
    if (!target || wowPath.empty() || !plan.contains("addonsDir")) {
        // Nothing to finish: the target is gone. Put the old profile back.
        try {
            rollbackPlan(profiles, plan);
        } catch (const std::exception&) {}
        std::error_code ec;
        fs::remove(file, ec);
        clearSwitchBackup();
        return false;
    }

    // A crash after the flags were saved but before the plan was deleted:
    // the switch had finished.
    const Profile* from = fromId ? profiles.find(fromId) : nullptr;
    if (target->active && !(from && from->active && samePath(from->wowPath, wowPath))) {
        std::error_code ec;
        fs::remove(file, ec);
        clearSwitchBackup();
        return true;
    }

    try {
        runPlan(profiles, plan);
    } catch (const std::exception&) {
        // A step failed for real (a zip that will not install, a full disk).
        // Put the old profile back so the switch is not stuck forever. If
        // that fails too, keep the plan: nothing more can be done here.
        const auto original = std::current_exception();
        try {
            rollbackPlan(profiles, plan);
        } catch (const std::exception&) {
            std::rethrow_exception(original);
        }
        std::error_code ec;
        fs::remove(file, ec);
        clearSwitchBackup();
        std::rethrow_exception(original);
    }

    // 5. Only now does the target own the folder.
    for (auto& p : profiles.profiles_) {
        if (p.id == targetId && !p.wowPath) p.wowPath = wowPath;
        if (samePath(p.wowPath, wowPath)) p.active = (p.id == targetId);
    }
    profiles.save();
    std::error_code ec;
    fs::remove(file, ec);
    clearSwitchBackup();
    return true;
}

} // namespace wam
