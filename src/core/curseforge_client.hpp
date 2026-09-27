#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <stdexcept>

namespace wam {

// CurseForge releaseType: 1 = release, 2 = beta, 3 = alpha
enum class ReleaseChannel { Release = 1, Beta = 2, Alpha = 3 };

struct CurseForgeMod {
    int64_t id = 0;
    std::string name;
    std::string slug;
    std::string summary;
    std::string websiteUrl;
};

struct CurseForgeFile {
    int64_t id = 0;
    int64_t modId = 0;
    std::string displayName;
    std::string fileName;
    ReleaseChannel releaseType = ReleaseChannel::Release;
    std::vector<std::string> gameVersions; // actual client version strings, e.g. "11.0.5" — NOT flavor names
    std::optional<std::string> downloadUrl; // nullopt => author blocked third-party downloads
    int64_t fileFingerprint = 0;
    std::vector<std::string> moduleNames; // top-level folders this file writes into AddOns/

    bool isBlocked() const { return !downloadUrl.has_value(); }
};

// A WoW "flavor" (Retail, Classic Era, Burning Crusade Classic, ...) as
// CurseForge models it: a game-version-type id under gameId=1. New classic
// re-releases add new ones over time, so this is discovered, never hardcoded.
struct GameVersionType {
    int64_t id = 0;
    std::string name; // e.g. "Retail", "Classic Era", "Burning Crusade Classic"
    std::string slug;
};

// Thin client around the public CurseForge API (https://api.curseforge.com/v1).
// Every call can fail (network, auth, rate limit) — callers must handle
// CurseForgeError; the rest of the app must keep working with no key at all.
class CurseForgeClient {
public:
    explicit CurseForgeClient(std::string apiKey);

    std::vector<CurseForgeMod> search(const std::string& query, int pageSize = 20);
    CurseForgeMod getMod(int64_t modId);

    // gameVersionTypeId, if given, is passed straight to the API's own
    // filter (server-side, authoritative) — see gameVersionTypeId() below
    // for turning a flavor name like "Retail" into this id.
    std::vector<CurseForgeFile> getFiles(int64_t modId, std::optional<int64_t> gameVersionTypeId = std::nullopt);
    CurseForgeFile getFile(int64_t modId, int64_t fileId);

    // Resolves a flavor name/substring (case-insensitive: "retail", "Retail",
    // "classic era", ...) to CurseForge's numeric gameVersionTypeId for WoW,
    // by discovering the live list from GET /v1/games/1/version-types and
    // caching it. Returns nullopt if nothing matches or discovery fails.
    std::optional<int64_t> gameVersionTypeId(const std::string& flavorSubstring);

    // Lists every flavor CurseForge currently knows about for WoW — useful
    // for telling the user what's valid when their --flavor doesn't match.
    std::vector<GameVersionType> listGameVersionTypes();

    // Picks the newest file matching the given channel. Files passed in
    // should already be flavor-filtered (via getFiles's gameVersionTypeId)
    // if a flavor was requested.
    static std::optional<CurseForgeFile> selectBestFile(
        const std::vector<CurseForgeFile>& files,
        ReleaseChannel channel);

    static const int64_t kWowGameId = 1;

private:
    std::string apiKey_;
    std::vector<std::string> authHeaders() const;

    // CurseForge classIds are global across every game on the site (not
    // per-game), so a hardcoded number is a guess that silently breaks the
    // moment it's wrong for this game. Instead we discover the numeric id of
    // WoW's "Addons" class from the API itself (GET /v1/categories) the
    // first time it's needed and cache it for the life of this client.
    // Returns nullopt (rather than throwing) on discovery failure, so search
    // can fall back to an unfiltered-by-class query instead of erroring out.
    std::optional<int64_t> addonsClassId();
    std::optional<int64_t> cachedAddonsClassId_;

    std::optional<std::vector<GameVersionType>> cachedGameVersionTypes_;
};

class CurseForgeError : public std::runtime_error {
public:
    CurseForgeError(long httpStatus, std::string message)
        : std::runtime_error(std::move(message)), status(httpStatus) {}
    long status;
};

} // namespace wam
