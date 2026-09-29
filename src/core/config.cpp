#include "core/config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

namespace wam {

using json = nlohmann::json;
namespace fs = std::filesystem;

static fs::path homeDir() {
    if (const char* h = std::getenv("HOME")) return fs::path(h);
    throw std::runtime_error("HOME environment variable not set");
}

fs::path Config::configPath() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return fs::path(xdg) / "wow-addon-manager" / "config.json";
    }
    return homeDir() / ".config" / "wow-addon-manager" / "config.json";
}

fs::path Config::dataDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        return fs::path(xdg) / "wow-addon-manager";
    }
    return homeDir() / ".local" / "share" / "wow-addon-manager";
}

Config Config::load() {
    Config cfg;
    auto path = configPath();
    if (!fs::exists(path)) return cfg; // no config yet: everything optional, still usable

    std::ifstream in(path);
    json j;
    in >> j;

    if (j.contains("curseforge_api_key") && !j["curseforge_api_key"].is_null())
        cfg.curseforge_api_key = j.at("curseforge_api_key").get<std::string>();
    if (j.contains("wow_path") && !j["wow_path"].is_null())
        cfg.wow_path = j.at("wow_path").get<std::string>();
    if (j.contains("wow_flavor_id") && j.at("wow_flavor_id").is_number_integer())
        cfg.wow_flavor_id = j.at("wow_flavor_id").get<int64_t>();
    if (j.contains("wow_flavor_name") && j.at("wow_flavor_name").is_string())
        cfg.wow_flavor_name = j.at("wow_flavor_name").get<std::string>();

    return cfg;
}

void Config::save() const {
    auto path = configPath();
    fs::create_directories(path.parent_path());

    json j;
    j["curseforge_api_key"] = curseforge_api_key.has_value() ? json(*curseforge_api_key) : json(nullptr);
    j["wow_path"] = wow_path.has_value() ? json(*wow_path) : json(nullptr);
    j["wow_flavor_id"] = wow_flavor_id.has_value() ? json(*wow_flavor_id) : json(nullptr);
    j["wow_flavor_name"] = wow_flavor_name.has_value() ? json(*wow_flavor_name) : json(nullptr);

    std::ofstream out(path);
    out << j.dump(2) << "\n";
}

std::string Config::wowFolderName() const {
    if (!wow_path.has_value()) return {};
    auto p = fs::path(*wow_path).lexically_normal();
    if (p.filename().empty()) p = p.parent_path(); // "/a/_retail_/" has an empty filename
    return p.filename().string();
}

fs::path Config::addonsDir() const {
    if (!wow_path.has_value())
        throw std::runtime_error("wow_path is not configured");
    return fs::path(*wow_path) / "Interface" / "AddOns";
}

} // namespace wam
