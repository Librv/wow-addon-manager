# CurseForge API

All calls go through `CurseForgeClient` (`src/core/curseforge_client.hpp`)
against `https://api.curseforge.com/v1`, WoW `gameId = 1`, with the user's API
key. Local operations (`list`, `remove`, `install-manual`, `scan`, `adopt`)
need no key.

## Endpoints in use

| Use | Endpoint | Source |
| --- | --- | --- |
| Addons class id | `GET /v1/categories?gameId=1&classesOnly=true` | API docs |
| Flavors | `GET /v1/games/1/version-types` | API docs |
| Search | `GET /v1/mods/search?gameId=1...` | API docs |
| One mod | `GET /v1/mods/{modId}` | API docs |
| Mods by id, batch (icons, authors) | `POST /v1/mods` | API docs |
| Files of a mod, paged | `GET /v1/mods/{modId}/files?pageSize=..&index=..` | API docs |
| One file | `GET /v1/mods/{modId}/files/{fileId}` | API docs |
| Files by id, batch (backfill) | `POST /v1/mods/files` | SDK docs only |
| Changelog | `GET /v1/mods/{modId}/files/{fileId}/changelog` | SDK docs only |
| Blocked-download link | `https://www.curseforge.com/api/v1/mods/<mod>/files/<file>/download` | the website's own endpoint, not the documented API; can change without notice |

The code was first written where `api.curseforge.com` was unreachable, so
none of these were exercised live at the time. When you confirm one against
a real response, note it here. The tests parse sample JSON, not live
responses: when you change parsing, add a sample to the tests and say in the
commit whether you checked it against the real API.

## Facts that are easy to get wrong

- **Class ids are global across all games**, not per game. `4471` is
  Minecraft Modpacks. Discover WoW's "Addons" class from `/v1/categories`.
- **Flavor is `gameVersionTypeId`**, a number. A file's `gameVersions` holds
  client versions like `"11.0.7"` and never contains "Retail". Filter on the
  server with `gameVersionTypeId`; never string-match flavor names.
- **`--flavor` matching** is case-insensitive against names and slugs; an exact
  match wins over a substring, so `classic` means "Classic".
- **Blocked downloads** show up as `downloadUrl == null`. The app then asks the
  user to download the zip and point wam at it.
- Fields the UI relies on: `logo.thumbnailUrl` (icon),
  `latestFilesIndexes[].gameVersionTypeId` (flavors a mod has), `fileDate`
  (version dates). A mod missing them gets a placeholder icon, every flavor,
  or no date.

## Open questions

- Page order of `/v1/mods/{id}/files` is not documented. The install window
  sorts loaded files newest first by id; if the API returns oldest first, a
  mod with more than 50 files for a flavor shows old versions first. Check
  with `GET /v1/mods/{id}/files?pageSize=5` on a mod with many files.
