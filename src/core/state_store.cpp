#include "core/state_store.hpp"
#include "core/config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace wam {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

json toJson(const InstalledAddon& a) {
    return json{
        {"modId", a.modId},
        {"fileId", a.fileId},
        {"displayName", a.displayName},
        {"fileName", a.fileName},
        {"channel", static_cast<int>(a.channel)},
        {"gameVersions", a.gameVersions},
        {"folders", a.folders},
        {"installedAt", a.installedAt},
        {"manuallyProvided", a.manuallyProvided},
        {"flavorTypeId", a.flavorTypeId},
        {"flavorName", a.flavorName},
        {"iconUrl", a.iconUrl},
        {"fileDisplayName", a.fileDisplayName},
        {"fileDate", a.fileDate},
        {"modSlug", a.modSlug},
        {"author", a.author},
        {"adopted", a.adopted},
    };
}

InstalledAddon fromJson(const json& j) {
    InstalledAddon a;
    a.modId = j.value("modId", int64_t{0});
    a.fileId = j.value("fileId", int64_t{0});
    a.displayName = j.value("displayName", "");
    a.fileName = j.value("fileName", "");
    a.channel = static_cast<ReleaseChannel>(j.value("channel", 1));
    if (j.contains("gameVersions"))
        a.gameVersions = j.at("gameVersions").get<std::vector<std::string>>();
    if (j.contains("folders"))
        a.folders = j.at("folders").get<std::vector<std::string>>();
    a.installedAt = j.value("installedAt", "");
    a.manuallyProvided = j.value("manuallyProvided", false);
    // Absent in files written before flavors were tracked: loads as unknown.
    a.flavorTypeId = j.value("flavorTypeId", int64_t{0});
    a.flavorName = j.value("flavorName", "");
    a.iconUrl = j.value("iconUrl", "");
    a.fileDisplayName = j.value("fileDisplayName", "");
    a.fileDate = j.value("fileDate", "");
    a.modSlug = j.value("modSlug", "");
    a.author = j.value("author", "");
    a.adopted = j.value("adopted", false);
    return a;
}

} // namespace

void recordFile(InstalledAddon& a, const CurseForgeFile& f) {
    a.fileId = f.id;
    a.fileName = f.fileName;
    a.fileDisplayName = f.displayName;
    a.fileDate = f.fileDate;
    a.channel = f.releaseType;
    a.gameVersions = f.gameVersions;
}

std::string nowIso8601() {
    auto t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

StateStore StateStore::load() {
    StateStore store;
    store.path_ = Config::dataDir() / "installed.json";
    if (!fs::exists(store.path_)) return store;

    std::ifstream in(store.path_);
    json j;
    in >> j;
    for (const auto& item : j.at("addons")) store.addons_.push_back(fromJson(item));
    return store;
}

void StateStore::save() const {
    fs::create_directories(path_.parent_path());
    json arr = json::array();
    for (const auto& a : addons_) arr.push_back(toJson(a));

    json root;
    root["addons"] = arr;

    std::ofstream out(path_);
    out << root.dump(2) << "\n";
}

void StateStore::upsert(const InstalledAddon& addon) {
    auto it = std::find_if(addons_.begin(), addons_.end(),
        [&](const InstalledAddon& a) { return a.modId == addon.modId; });
    if (it != addons_.end()) *it = addon;
    else addons_.push_back(addon);
}

bool StateStore::remove(int64_t modId) {
    auto before = addons_.size();
    addons_.erase(std::remove_if(addons_.begin(), addons_.end(),
        [&](const InstalledAddon& a) { return a.modId == modId; }), addons_.end());
    return addons_.size() != before;
}

std::optional<InstalledAddon> StateStore::find(int64_t modId) const {
    auto it = std::find_if(addons_.begin(), addons_.end(),
        [&](const InstalledAddon& a) { return a.modId == modId; });
    if (it == addons_.end()) return std::nullopt;
    return *it;
}

} // namespace wam
