#include "core/reconciler.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace wam {

namespace fs = std::filesystem;

std::vector<ScanEntry> Reconciler::scan(const fs::path& addonsDir, const StateStore& state) {
    std::vector<ScanEntry> results;

    std::unordered_set<std::string> tracked;
    for (const auto& addon : state.all())
        for (const auto& folder : addon.folders) tracked.insert(folder);

    std::error_code ec;
    if (!fs::exists(addonsDir, ec) || !fs::is_directory(addonsDir, ec)) return results;

    for (const auto& entry : fs::directory_iterator(addonsDir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.rfind(".wam-", 0) == 0) continue; // wam's own staging/backup folders
        if (tracked.count(name)) continue;

        ScanEntry se;
        se.folder = name;
        se.toc = TocReader::readFolder(entry.path());
        results.push_back(std::move(se));
    }

    std::sort(results.begin(), results.end(),
        [](const ScanEntry& a, const ScanEntry& b) { return a.folder < b.folder; });
    return results;
}

std::vector<std::pair<int64_t, std::vector<ScanEntry>>> Reconciler::groupByModId(
    const std::vector<ScanEntry>& entries) {
    std::vector<std::pair<int64_t, std::vector<ScanEntry>>> groups;

    for (const auto& e : entries) {
        int64_t key = e.toc.curseProjectId.value_or(0);
        auto it = std::find_if(groups.begin(), groups.end(),
            [&](const auto& g) { return g.first == key; });
        if (it != groups.end()) it->second.push_back(e);
        else groups.push_back({key, {e}});
    }

    // Untagged (key 0) last, so a scan report leads with actionable matches.
    std::stable_sort(groups.begin(), groups.end(),
        [](const auto& a, const auto& b) {
            if ((a.first == 0) != (b.first == 0)) return a.first != 0;
            return a.first < b.first;
        });
    return groups;
}

InstalledAddon Reconciler::adopt(StateStore& state, const fs::path& addonsDir, int64_t modId,
                                 const std::string& displayName, const std::string& iconUrl,
                                 const std::vector<std::string>& folders) {
    if (modId <= 0) throw std::runtime_error("invalid mod id " + std::to_string(modId));
    if (folders.empty()) throw std::runtime_error("no folders to adopt");

    for (const auto& f : folders) {
        if (f.empty() || f == "." || f.find("..") != std::string::npos ||
            f.find('/') != std::string::npos || f.find('\\') != std::string::npos ||
            f.rfind(".wam-", 0) == 0)
            throw std::runtime_error("not a plain AddOns folder name: '" + f + "'");

        std::error_code ec;
        if (!fs::is_directory(addonsDir / f, ec))
            throw std::runtime_error("no such folder in AddOns: " + f);

        // Never silently move a folder that another tracked addon already owns.
        for (const auto& a : state.all()) {
            if (a.modId == modId) continue;
            if (std::find(a.folders.begin(), a.folders.end(), f) != a.folders.end())
                throw std::runtime_error("'" + f + "' is already tracked under mod " +
                                         std::to_string(a.modId) + " (" + a.displayName + ")");
        }
    }

    auto existing = state.find(modId);
    InstalledAddon rec = existing.value_or(InstalledAddon{});
    if (!existing.has_value()) {
        rec.modId = modId;
        rec.fileId = 0; // unknown version: this came from a .toc tag, not a download
        rec.displayName = displayName;
        rec.channel = ReleaseChannel::Release;
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
        rec.adopted = true;
    }
    if (rec.iconUrl.empty()) rec.iconUrl = iconUrl;
    for (const auto& f : folders)
        if (std::find(rec.folders.begin(), rec.folders.end(), f) == rec.folders.end())
            rec.folders.push_back(f);

    state.upsert(rec);
    return rec;
}

} // namespace wam
