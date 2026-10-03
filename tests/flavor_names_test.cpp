// Checks for the default flavor display names (a leading "WoW" is dropped).
// Same hand-rolled style as test_main.cpp.

#include "core/flavor_cache.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace wam;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::cerr << "FAIL: " << what << "\n"; }
    else std::cout << "ok:   " << what << "\n";
}

void testDefaultName() {
    check(FlavorCache::defaultName("WoW Forever") == "Forever" && FlavorCache::defaultName("wow-forever") == "Forever" &&
          FlavorCache::defaultName("WOW_Forever") == "Forever" && FlavorCache::defaultName("WoW - Forever") == "Forever",
          "defaultName drops a leading WoW and its separators");
    check(FlavorCache::defaultName("Retail") == "Retail" && FlavorCache::defaultName("Classic Era") == "Classic Era" &&
          FlavorCache::defaultName("Wowza") == "Wowza" && FlavorCache::defaultName("WoW") == "WoW" &&
          FlavorCache::defaultName("WoW ") == "WoW " && FlavorCache::defaultName("") == "",
          "defaultName leaves everything else alone, including a name that would become empty");
}

void testMerge() {
    FlavorCache c;
    c.merge({{40, "WoW Forever", "wow-forever"}, {10, "Retail", "wow-retail"}});
    check(c.nameFor(40) == "Forever" && c.findById(40)->apiName == "WoW Forever" && c.nameFor(10) == "Retail",
          "a new flavor shows without the WoW prefix and keeps CurseForge's own name");

    c.rename("wow-forever", "Ever");
    c.merge({{40, "WoW Forever", "wow-forever"}});
    check(c.nameFor(40) == "Ever", "an edited name survives a merge");

    c.rename("wow-forever", "");
    check(c.nameFor(40) == "Forever", "an empty rename resets to the default, prefix-free name");

    c.merge({{40, "WoW Forever II", "wow-forever"}});
    check(c.nameFor(40) == "Forever II", "an unedited name follows a rename upstream, still without the prefix");
}

void testMigration(const fs::path& dir) {
    // A flavors.json written before the prefix was dropped: full names, never edited, plus one edited.
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
    fs::create_directories(FlavorCache::path().parent_path());
    std::ofstream(FlavorCache::path()) <<
        R"({"wow-forever": {"id": 40, "name": "WoW Forever", "api_name": "WoW Forever"},
            "wow-retail": {"id": 10, "name": "Live", "api_name": "WoW Retail"}})";
    auto c = FlavorCache::load();
    check(c.nameFor(40) == "WoW Forever", "loading alone leaves a stored name as it is");
    c.merge({{40, "WoW Forever", "wow-forever"}, {10, "WoW Retail", "wow-retail"}});
    check(c.nameFor(40) == "Forever", "an old, unedited full name migrates on the next refresh");
    check(c.nameFor(10) == "Live", "an old edited name is kept");

    // Hand-written short form with no names at all falls back to the slug, also prefix-free.
    std::ofstream(FlavorCache::path()) << R"({"wow-forever": ""})";
    check(FlavorCache::load().nameFor(0) == "Forever", "a bare slug entry shows without the prefix");
}

} // namespace

int main() {
    auto dir = fs::temp_directory_path() / "wam_flavor_names_test";
    fs::remove_all(dir);
    testDefaultName();
    testMerge();
    testMigration(dir);
    fs::remove_all(dir);
    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
