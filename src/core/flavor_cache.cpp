#include "core/flavor_cache.hpp"
#include "core/config.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace wam {

using ordered_json = nlohmann::ordered_json;
namespace fs = std::filesystem;

fs::path FlavorCache::path() {
    return Config::configPath().parent_path() / "flavors.json";
}

std::string FlavorCache::keyFor(const GameVersionType& t) {
    if (!t.slug.empty()) return t.slug;
    std::string out;
    for (unsigned char c : t.name) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "flavor-" + std::to_string(t.id) : out;
}

std::string FlavorCache::defaultName(const std::string& apiName) {
    auto isSep = [](char c) { return c == ' ' || c == '-' || c == '_'; };
    auto lower = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
    if (apiName.size() > 4 && isSep(apiName[3]) &&
        lower(apiName[0]) == 'w' && lower(apiName[1]) == 'o' && lower(apiName[2]) == 'w') {
        size_t i = 3;
        while (i < apiName.size() && isSep(apiName[i])) ++i;
        if (i < apiName.size()) {
            std::string rest = apiName.substr(i);
            rest[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(rest[0])));
            return rest;
        }
    }
    return apiName;
}

FlavorCache FlavorCache::load() {
    FlavorCache cache;
    try {
        std::ifstream in(path());
        if (!in) return cache;
        ordered_json j;
        in >> j;
        if (!j.is_object()) return cache;
        for (const auto& [slug, value] : j.items()) {
            FlavorEntry e;
            e.slug = slug;
            if (value.is_string()) {
                // Hand-written short form, "wow-forever": "Forever". The id and
                // CurseForge's name are filled in by the next merge().
                e.name = value.get<std::string>();
            } else if (value.is_object()) {
                if (value.contains("id") && value.at("id").is_number_integer()) e.id = value.at("id").get<int64_t>();
                if (value.contains("name") && value.at("name").is_string()) e.name = value.at("name").get<std::string>();
                if (value.contains("api_name") && value.at("api_name").is_string()) e.apiName = value.at("api_name").get<std::string>();
            } else {
                continue;
            }
            if (e.name.empty()) e.name = defaultName(e.apiName.empty() ? e.slug : e.apiName);
            cache.entries_.push_back(std::move(e));
        }
    } catch (const std::exception&) {
        return FlavorCache{}; // a damaged file is rebuilt from CurseForge at the next start
    }
    return cache;
}

void FlavorCache::save() const {
    ordered_json j = ordered_json::object();
    for (const auto& e : entries_)
        j[e.slug] = {{"id", e.id}, {"name", e.name}, {"api_name", e.apiName}};
    fs::create_directories(path().parent_path());
    std::ofstream out(path());
    out << j.dump(2) << "\n";
}

void FlavorCache::merge(const std::vector<GameVersionType>& live) {
    for (const auto& t : live) {
        const std::string key = keyFor(t);
        auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const FlavorEntry& e) { return e.slug == key; });
        if (it == entries_.end()) {
            entries_.push_back({key, t.id, defaultName(t.name), t.name});
            continue;
        }
        // Edited = the display name is neither what CurseForge called it last
        // time nor the default form of that. (A cache written before the
        // prefix was dropped holds the full name, so it migrates here.)
        const std::string& prev = it->apiName.empty() ? t.name : it->apiName;
        const bool edited = it->name != prev && it->name != defaultName(prev);
        it->id = t.id;
        it->apiName = t.name;
        if (!edited) it->name = defaultName(t.name);
    }
}

bool FlavorCache::rename(const std::string& slug, const std::string& name) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const FlavorEntry& e) { return e.slug == slug; });
    if (it == entries_.end()) return false;
    it->name = name.empty() ? defaultName(it->apiName.empty() ? it->slug : it->apiName) : name;
    return true;
}

std::optional<FlavorEntry> FlavorCache::findById(int64_t id) const {
    for (const auto& e : entries_) if (e.id == id) return e;
    return std::nullopt;
}

std::string FlavorCache::nameFor(int64_t id, const std::string& fallback) const {
    auto e = findById(id);
    return e ? e->name : fallback;
}

std::vector<GameVersionType> FlavorCache::asTypes(bool apiNames) const {
    std::vector<GameVersionType> out;
    for (const auto& e : entries_)
        out.push_back({e.id, apiNames && !e.apiName.empty() ? e.apiName : e.name, e.slug});
    return out;
}

} // namespace wam
