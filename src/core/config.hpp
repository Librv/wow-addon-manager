#pragma once
#include <optional>
#include <string>
#include <filesystem>

namespace wam {

// App configuration. Must remain fully optional: the app has to stay usable
// for managing already-installed addons with no API key and no network.
struct Config {
    std::optional<std::string> curseforge_api_key;
    std::optional<std::string> wow_path; // manually specified install folder

    // Where config.json lives: $XDG_CONFIG_HOME/wow-addon-manager/config.json
    // or ~/.config/wow-addon-manager/config.json
    static std::filesystem::path configPath();

    // Where installed.json / state lives: $XDG_DATA_HOME/wow-addon-manager/
    // or ~/.local/share/wow-addon-manager/
    static std::filesystem::path dataDir();

    // Returns default-constructed Config if no config file exists yet.
    static Config load();

    void save() const;

    // Convenience: <wow_path>/Interface/AddOns, throws if wow_path unset.
    std::filesystem::path addonsDir() const;
};

} // namespace wam
