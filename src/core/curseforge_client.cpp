#include "core/curseforge_client.hpp"
#include "core/http_client.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace wam {

using json = nlohmann::json;

namespace {

constexpr const char* kBaseUrl = "https://api.curseforge.com/v1";

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

bool containsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

std::string stringOrEmpty(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j.at(key).is_string()) return j.at(key).get<std::string>();
    return {};
}

int intOrDefault(const json& j, const char* key, int fallback) {
    if (j.is_object() && j.contains(key) && j.at(key).is_number_integer()) return j.at(key).get<int>();
    return fallback;
}

// Lowercases and turns every run of non-alphanumerics into one space:
// "_classic_era_" and "classic-era" both become "classic era".
std::string normalizeWords(const std::string& s) {
    std::string out;
    bool space = true; // no leading space
    for (unsigned char c : s) {
        if (std::isalnum(c)) { out += static_cast<char>(std::tolower(c)); space = false; }
        else if (!space) { out += ' '; space = true; }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

CurseForgeFile parseFile(const json& f) {
    CurseForgeFile file;
    file.id = f.at("id").get<int64_t>();
    file.modId = f.at("modId").get<int64_t>();
    file.displayName = f.value("displayName", "");
    file.fileName = f.value("fileName", "");
    file.releaseType = static_cast<ReleaseChannel>(f.value("releaseType", 1));
    file.fileFingerprint = f.value("fileFingerprint", int64_t{0});
    file.fileDate = stringOrEmpty(f, "fileDate");

    if (f.contains("downloadUrl") && !f.at("downloadUrl").is_null())
        file.downloadUrl = f.at("downloadUrl").get<std::string>();

    if (f.contains("gameVersions") && f.at("gameVersions").is_array())
        for (const auto& gv : f.at("gameVersions")) file.gameVersions.push_back(gv.get<std::string>());

    if (f.contains("modules") && f.at("modules").is_array())
        for (const auto& m : f.at("modules"))
            if (m.contains("name")) file.moduleNames.push_back(m.at("name").get<std::string>());

    return file;
}

CurseForgeMod parseMod(const json& m) {
    CurseForgeMod mod;
    mod.id = m.at("id").get<int64_t>();
    mod.name = m.value("name", "");
    mod.slug = m.value("slug", "");
    mod.summary = m.value("summary", "");
    if (m.contains("links") && m.at("links").contains("websiteUrl") && !m["links"]["websiteUrl"].is_null())
        mod.websiteUrl = m["links"]["websiteUrl"].get<std::string>();

    if (m.contains("logo") && m.at("logo").is_object()) {
        // Prefer the thumbnail: icons are shown small, and it is much lighter than the full logo.
        mod.logoUrl = stringOrEmpty(m.at("logo"), "thumbnailUrl");
        if (mod.logoUrl.empty()) mod.logoUrl = stringOrEmpty(m.at("logo"), "url");
    }

    if (m.contains("latestFilesIndexes") && m.at("latestFilesIndexes").is_array()) {
        for (const auto& idx : m.at("latestFilesIndexes")) {
            if (!idx.contains("gameVersionTypeId") || idx.at("gameVersionTypeId").is_null()) continue;
            auto id = idx.at("gameVersionTypeId").get<int64_t>();
            if (std::find(mod.gameVersionTypeIds.begin(), mod.gameVersionTypeIds.end(), id)
                    == mod.gameVersionTypeIds.end())
                mod.gameVersionTypeIds.push_back(id);
        }
    }
    return mod;
}

[[noreturn]] void throwFor(const HttpResponse& r, const std::string& context) {
    std::ostringstream msg;
    msg << context << " failed (HTTP " << r.status << "): " << r.body;
    throw CurseForgeError(r.status, msg.str());
}

} // namespace

CurseForgeClient::CurseForgeClient(std::string apiKey) : apiKey_(std::move(apiKey)) {}

std::vector<std::string> CurseForgeClient::authHeaders() const {
    return {
        "x-api-key: " + apiKey_,
        "Accept: application/json",
    };
}

std::optional<int64_t> CurseForgeClient::addonsClassId() {
    if (cachedAddonsClassId_.has_value()) return cachedAddonsClassId_;

    std::ostringstream url;
    url << kBaseUrl << "/categories?gameId=" << kWowGameId << "&classesOnly=true";
    auto resp = HttpClient::get(url.str(), authHeaders());
    if (!resp.ok()) return std::nullopt; // discovery failure: let callers fall back gracefully

    try {
        json j = json::parse(resp.body);
        for (const auto& c : j.at("data")) {
            bool isClass = c.value("isClass", false);
            std::string slug = c.value("slug", "");
            std::string name = c.value("name", "");
            if (isClass && (slug == "addons" || name == "Addons")) {
                cachedAddonsClassId_ = c.at("id").get<int64_t>();
                return cachedAddonsClassId_;
            }
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<GameVersionType> CurseForgeClient::listGameVersionTypes() {
    if (cachedGameVersionTypes_.has_value()) return *cachedGameVersionTypes_;

    std::ostringstream url;
    url << kBaseUrl << "/games/" << kWowGameId << "/version-types";
    auto resp = HttpClient::get(url.str(), authHeaders());
    if (!resp.ok()) throwFor(resp, "listGameVersionTypes()");

    json j = json::parse(resp.body);
    std::vector<GameVersionType> out;
    for (const auto& t : j.at("data")) {
        GameVersionType gvt;
        gvt.id = t.at("id").get<int64_t>();
        gvt.name = t.value("name", "");
        gvt.slug = t.value("slug", "");
        out.push_back(gvt);
    }
    cachedGameVersionTypes_ = out;
    return out;
}

std::optional<int64_t> CurseForgeClient::matchFlavor(const std::vector<GameVersionType>& types,
                                                      const std::string& flavorSubstring) {
    const std::string want = toLower(flavorSubstring);
    // Exact name/slug first: "classic" must not be shadowed by whichever
    // "... Classic" flavor happens to be listed before it.
    for (const auto& t : types)
        if (toLower(t.name) == want || toLower(t.slug) == want) return t.id;
    for (const auto& t : types)
        if (containsCaseInsensitive(t.name, flavorSubstring) || containsCaseInsensitive(t.slug, flavorSubstring))
            return t.id;
    return std::nullopt;
}

std::optional<int64_t> CurseForgeClient::matchFlavorForFolder(const std::vector<GameVersionType>& types,
                                                               const std::string& folderName) {
    std::istringstream words(normalizeWords(folderName));
    std::string word, key;
    bool testRealm = false;
    while (words >> word) {
        if (word == "ptr" || word == "xptr" || word == "beta" || word == "alpha") { testRealm = true; continue; }
        key += (key.empty() ? "" : " ") + word;
    }
    // A bare "_ptr_", "_xptr_" or "_beta_" is the test realm of the main game.
    if (key.empty() && testRealm) key = "retail";
    if (key.empty()) return std::nullopt;
    for (const auto& t : types)
        if (normalizeWords(t.name) == key || normalizeWords(t.slug) == key) return t.id;
    return std::nullopt;
}

std::optional<int64_t> CurseForgeClient::gameVersionTypeId(const std::string& flavorSubstring) {
    try {
        return matchFlavor(listGameVersionTypes(), flavorSubstring);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<CurseForgeMod> CurseForgeClient::search(const std::string& query, int pageSize) {
    std::ostringstream url;
    url << kBaseUrl << "/mods/search?gameId=" << kWowGameId;

    if (auto classId = addonsClassId(); classId.has_value()) {
        url << "&classId=" << *classId;
    }
    // If discovery failed, we still search: unfiltered by class rather
    // than silently returning zero results forever.

    url << "&searchFilter=" << urlEncode(query)
        << "&pageSize=" << pageSize
        << "&sortField=2&sortOrder=desc"; // sortField 2 = popularity

    auto resp = HttpClient::get(url.str(), authHeaders());
    if (!resp.ok()) throwFor(resp, "search('" + query + "')");

    return parseModList(resp.body);
}

std::vector<CurseForgeMod> CurseForgeClient::parseModList(const std::string& jsonBody) {
    json j = json::parse(jsonBody);
    std::vector<CurseForgeMod> out;
    for (const auto& m : j.at("data")) out.push_back(parseMod(m));
    return out;
}

CurseForgeMod CurseForgeClient::parseModObject(const std::string& jsonBody) {
    json j = json::parse(jsonBody);
    return parseMod(j.at("data"));
}

CurseForgeMod CurseForgeClient::getMod(int64_t modId) {
    std::ostringstream url;
    url << kBaseUrl << "/mods/" << modId;
    auto resp = HttpClient::get(url.str(), authHeaders());
    if (!resp.ok()) throwFor(resp, "getMod(" + std::to_string(modId) + ")");
    return parseModObject(resp.body);
}

std::vector<CurseForgeMod> CurseForgeClient::getMods(const std::vector<int64_t>& modIds) {
    if (modIds.empty()) return {};
    json body;
    body["modIds"] = modIds;
    auto headers = authHeaders();
    headers.push_back("Content-Type: application/json");
    auto resp = HttpClient::post(std::string(kBaseUrl) + "/mods", body.dump(), headers);
    if (!resp.ok()) throwFor(resp, "getMods()");
    return parseModList(resp.body);
}

std::vector<CurseForgeFile> CurseForgeClient::getFiles(int64_t modId, std::optional<int64_t> gameVersionTypeId) {
    return getFilesPage(modId, gameVersionTypeId, 0, 50).files;
}

FilesPage CurseForgeClient::getFilesPage(int64_t modId, std::optional<int64_t> gameVersionTypeId,
                                         int index, int pageSize) {
    std::ostringstream url;
    url << kBaseUrl << "/mods/" << modId << "/files?pageSize=" << pageSize << "&index=" << index;
    if (gameVersionTypeId.has_value()) url << "&gameVersionTypeId=" << *gameVersionTypeId;

    auto resp = HttpClient::get(url.str(), authHeaders());
    if (!resp.ok()) throwFor(resp, "getFiles(" + std::to_string(modId) + ")");
    return parseFilesPage(resp.body);
}

FilesPage CurseForgeClient::parseFilesPage(const std::string& jsonBody) {
    json j = json::parse(jsonBody);
    FilesPage page;
    for (const auto& f : j.at("data")) page.files.push_back(parseFile(f));
    page.totalCount = static_cast<int>(page.files.size());
    if (j.contains("pagination") && j.at("pagination").is_object()) {
        const auto& pg = j.at("pagination");
        page.index = intOrDefault(pg, "index", 0);
        page.totalCount = intOrDefault(pg, "totalCount", page.totalCount);
    }
    return page;
}

CurseForgeFile CurseForgeClient::getFile(int64_t modId, int64_t fileId) {
    std::ostringstream url;
    url << kBaseUrl << "/mods/" << modId << "/files/" << fileId;
    auto resp = HttpClient::get(url.str(), authHeaders());
    if (!resp.ok()) throwFor(resp, "getFile(" + std::to_string(modId) + "," + std::to_string(fileId) + ")");
    json j = json::parse(resp.body);
    return parseFile(j.at("data"));
}

std::optional<CurseForgeFile> CurseForgeClient::selectBestFile(
    const std::vector<CurseForgeFile>& files,
    ReleaseChannel channel) {

    std::optional<CurseForgeFile> best;
    for (const auto& f : files) {
        // A file's own releaseType is its stability; CurseForge channels
        // aren't ordered (beta isn't "more stable release"), so match exactly.
        if (f.releaseType != channel) continue;
        if (!best.has_value() || f.id > best->id) best = f; // higher id ~= newer upload
    }
    return best;
}

} // namespace wam
