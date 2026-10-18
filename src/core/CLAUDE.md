# src/core rules

`wam_core`: plain C++20 plus libcurl, nlohmann/json, libzip and OpenSSL. The
repo-wide rules in `/CLAUDE.md` apply too.

- **No Qt.** The CLI and the tests link this without Qt.
- **Calls block.** Every function here may do network or disk work. Callers in
  the GUI reach it only through `WamWorker`, never from the GUI thread.
- **CurseForge:** go through `CurseForgeClient`. Ids (Addons class, flavors)
  are discovered and cached for the process lifetime, never hardcoded. Check
  `docs/curseforge-api.md` for which endpoints are confirmed and which are not.
- **Installs:** `AddonInstaller::installZip` is the only way into `AddOns/`.
  Keep its order: extract into `.wam-staging`, move old folders into
  `.wam-backup`, rename staged folders in, delete the backup. A leftover
  `.wam-backup` means an interrupted swap and is restored on the next run.
  `.wam-*` folders are never reported by `scan`.
- **Folder names** from state or zips must be one plain path component; keep
  the checks in `removeFolders` and `installZip`.
- **State** (`StateStore`, `installed.json`): new fields get a default that
  means "unknown", documented in a trailing comment, and older files still
  load. `fileId == 0` means adopted with no file yet; `flavorTypeId == 0`
  means unknown flavor.
- **Tests** for core code go in `tests/test_main.cpp` (or
  `tests/flavor_names_test.cpp` for flavor naming), with sample JSON instead of
  live API calls.
