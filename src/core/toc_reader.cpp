#include "core/toc_reader.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace wam {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Strips a UTF-8 BOM if the line is the file's first and starts with one.
std::string stripBom(std::string s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

void applyField(TocMetadata& meta, const std::string& keyLower, const std::string& value) {
    if (value.empty()) return;
    if (keyLower == "x-curse-project-id") {
        try {
            meta.curseProjectId = std::stoll(value);
        } catch (...) {
            // Non-numeric value in the field: leave unset rather than guess.
        }
    } else if (keyLower == "x-wowi-id" && !meta.wowiId) {
        meta.wowiId = value;
    } else if (keyLower == "x-wago-id" && !meta.wagoId) {
        meta.wagoId = value;
    } else if (keyLower == "version" && !meta.version) {
        meta.version = value;
    } else if (keyLower == "title" && !meta.title) {
        meta.title = value;
    }
}

} // namespace

TocMetadata TocReader::parseFile(const fs::path& tocPath) {
    TocMetadata meta;
    std::ifstream in(tocPath, std::ios::binary);
    if (!in) return meta;

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            line = stripBom(line);
            first = false;
        }
        std::string trimmed = trim(line);
        // TOC metadata lines look like "## Key: Value". A single '#' is a
        // plain comment and not metadata.
        if (trimmed.size() < 2 || trimmed[0] != '#' || trimmed[1] != '#') continue;

        std::string rest = trimmed.substr(2);
        auto colon = rest.find(':');
        if (colon == std::string::npos) continue;

        std::string key = toLower(trim(rest.substr(0, colon)));
        std::string value = trim(rest.substr(colon + 1));
        applyField(meta, key, value);
    }
    return meta;
}

TocMetadata TocReader::readFolder(const fs::path& folderPath) {
    TocMetadata merged;
    std::error_code ec;
    if (!fs::exists(folderPath, ec) || !fs::is_directory(folderPath, ec)) return merged;

    for (const auto& entry : fs::directory_iterator(folderPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (toLower(entry.path().extension().string()) != ".toc") continue;

        TocMetadata fileMeta = parseFile(entry.path());
        if (!merged.curseProjectId) merged.curseProjectId = fileMeta.curseProjectId;
        if (!merged.wowiId) merged.wowiId = fileMeta.wowiId;
        if (!merged.wagoId) merged.wagoId = fileMeta.wagoId;
        if (!merged.version) merged.version = fileMeta.version;
        if (!merged.title) merged.title = fileMeta.title;
    }
    return merged;
}

} // namespace wam
