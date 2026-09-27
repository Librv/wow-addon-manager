#include "core/reconciler.hpp"
#include <algorithm>
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

} // namespace wam
