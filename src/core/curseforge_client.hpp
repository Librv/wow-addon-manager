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
    // Flavors (gameVersionTypeIds) this mod has files for, taken from the
    // mod's latestFilesIndexes. Lets a UI offer only flavors that exist.
    std::vector<int64_t> gameVersionTypeIds;
    // Addon icon: the mod's logo thumbnail URL (empty if it has none).
    std::string logoUrl;
    // The mod's author names from `authors`, joined with ", " (empty if none).
    std::string author;
    // Total downloads (0 if the API gave none).
    int64_t downloadCount = 0;
    // The newest of the mod's `latestFiles` (highest file id): its version
    // name (displayName, else fileName) and upload date. The date falls back
    // to the mod's dateReleased/dateModified. Both empty if unknown.
    std::string latestVersion;
    std::string latestDate;
};

struct CurseForgeFile {
    int64_t id = 0;
    int64_t modId = 0;
    std::string displayName;
    std::string fileName;
    ReleaseChannel releaseType = ReleaseChannel::Release;
    std::vector<std::string> gameVersions; // actual client version strings, e.g. "11.0.5", NOT flavor names
    std::optional<std::string> downloadUrl; // nullopt => author blocked third-party downloads
    int64_t fileFingerprint = 0;
    std::string fileDate; // ISO-8601 upload time as CurseForge reports it, e.g. "2026-09-29T14:03:11.5Z"
    std::vector<std::string> moduleNames; // top-level folders this file writes into AddOns/

    bool isBlocked() const { return !downloadUrl.has_value(); }
};

// One page of a mod's file list. The API pages with index/pageSize and
// reports the total, so a caller can ask for the next page on demand.
struct FilesPage {
    std::vector<CurseForgeFile> files;
    int index = 0;      // offset of the first file in this page
    int totalCount = 0; // files matching the filter across every page
    bool hasMore() const { return index + static_cast<int>(files.size()) < totalCount; }
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
// Every call can fail (network, auth, rate limit); callers must handle
// CurseForgeError; the rest of the app must keep working with no key at all.
class CurseForgeClient {
public:
    explicit CurseForgeClient(std::string apiKey);

    std::vector<CurseForgeMod> search(const std::string& query, int pageSize = 20);
    CurseForgeMod getMod(int64_t modId);

    // Several mods in one request (POST /v1/mods). Order is not guaranteed
    // and unknown ids are simply absent from the result.
    std::vector<CurseForgeMod> getMods(const std::vector<int64_t>& modIds);

    // gameVersionTypeId, if given, is passed straight to the API's own
    // filter (server-side, authoritative). See gameVersionTypeId() below
    // for turning a flavor name like "Retail" into this id.
    std::vector<CurseForgeFile> getFiles(int64_t modId, std::optional<int64_t> gameVersionTypeId = std::nullopt);
    CurseForgeFile getFile(int64_t modId, int64_t fileId);

    // Several files at once, across any mods (POST /v1/mods/files). Unknown
    // ids are simply absent from the result.
    std::vector<CurseForgeFile> getFilesByIds(const std::vector<int64_t>& fileIds);

    // A file's changelog, as the HTML CurseForge stores (see htmlToPlainText).
    std::string getFileChangelog(int64_t modId, int64_t fileId);

    // One page of the file list (the API allows at most 50 per page). The
    // order within and across pages is whatever the API returns; callers that
    // need newest-first sort what they have loaded.
    FilesPage getFilesPage(int64_t modId, std::optional<int64_t> gameVersionTypeId,
                           int index = 0, int pageSize = 50);

    // Resolves a flavor name/slug (case-insensitive: "retail", "Retail",
    // "classic era", ...) to CurseForge's numeric gameVersionTypeId for WoW,
    // by discovering the live list from GET /v1/games/1/version-types and
    // caching it. An exact name/slug match wins over a substring match.
    // Returns nullopt if nothing matches or discovery fails.
    std::optional<int64_t> gameVersionTypeId(const std::string& flavorSubstring);

    // The matching rule above, on an already-fetched list (pure, testable).
    static std::optional<int64_t> matchFlavor(const std::vector<GameVersionType>& types,
                                               const std::string& flavorSubstring);

    // Works out which flavor a WoW install folder belongs to from its name:
    // "_retail_" -> Retail, "_classic_era_" -> Classic Era. Test and preview
    // realms ("_ptr_", "_xptr_", "_beta_", "_classic_ptr_") share the live
    // flavor's addons, so those words are ignored (a bare one means Retail). Only an exact name or slug
    // match counts; an ambiguous folder such as "_classic_" resolves only if a
    // flavor is literally called "Classic", otherwise nullopt (never a guess).
    static std::optional<int64_t> matchFlavorForFolder(const std::vector<GameVersionType>& types,
                                                        const std::string& folderName);

    // Lists every flavor CurseForge currently knows about for WoW, useful
    // for telling the user what's valid when their --flavor doesn't match.
    std::vector<GameVersionType> listGameVersionTypes();

    // Picks the newest file matching the given channel. Files passed in
    // should already be flavor-filtered (via getFiles's gameVersionTypeId)
    // if a flavor was requested.
    static std::optional<CurseForgeFile> selectBestFile(
        const std::vector<CurseForgeFile>& files,
        ReleaseChannel channel);

    // Response-body parsers, exposed so they can be tested without network.
    // parseModList: {"data":[mod,...]}   parseModObject: {"data":mod}
    static std::vector<CurseForgeMod> parseModList(const std::string& jsonBody);
    static CurseForgeMod parseModObject(const std::string& jsonBody);
    // parseFilesPage: {"data":[file,...],"pagination":{"index":0,"totalCount":N,...}}
    static FilesPage parseFilesPage(const std::string& jsonBody);
    // parseFileList: {"data":[file,...]}   parseChangelog: {"data":"<html>"}
    static std::vector<CurseForgeFile> parseFileList(const std::string& jsonBody);
    static std::string parseChangelog(const std::string& jsonBody);

    // Where a person's browser gets a file from, for downloads the author has
    // blocked for third-party tools. This is the website's own endpoint, not
    // part of the documented API.
    static std::string browserDownloadUrl(int64_t modId, int64_t fileId);
    // The addon's page on the website.
    static std::string modPageUrl(const std::string& slug);

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
