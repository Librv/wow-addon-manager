# wow-addon-manager

A WoW addon manager for Linux. The engine (`wam_core`) talks to CurseForge,
installs and updates addons, and keeps its own record of what it put where.
It has two front ends over that same core:

- `wam`: a CLI covering everything the core can do
- `wam-gui`: a Qt 6 / Kirigami desktop app (a sidebar with AddOns, Search and
  Settings; scanning for addons you already have and an update review, both as
  popups; manual install for blocked downloads), with each addon's CurseForge
  icon shown in the lists

## Build

Core dependencies (Ubuntu/Debian package names, with CachyOS/Arch
equivalents in parentheses): `cmake`, `libcurl4-openssl-dev` (`curl`),
`nlohmann-json3-dev` (`nlohmann-json`), `libzip-dev` (`libzip`),
`libssl-dev` (`openssl`).

GUI dependencies: Qt 6.5 or newer (`qt6-base`, `qt6-declarative`) to build,
plus Kirigami and the KDE Qt Quick Controls style at runtime (`kirigami`,
`qqc2-desktop-style` on Arch; package names from memory, check with your
package manager). Qt 6.5 is a hard floor because `main.cpp` uses
`QQmlApplicationEngine::loadFromModule`.

```sh
cmake -S . -B build
cmake --build build -j
```

The GUI is on by default. To build only the CLI and tests, with no Qt
needed:

```sh
cmake -S . -B build -DWAM_BUILD_GUI=OFF
```

Binaries:
- `build/wam`: the CLI
- `build/wam-gui`: the GUI (unless `WAM_BUILD_GUI=OFF`)
- `build/tests/wam_tests`: offline core tests (no network or API key)
- `build/tests/wam_gui_tests`: offline tests for the Qt adapter layer

```sh
ctest --test-dir build --output-on-failure
```

If you update an existing checkout in place and the link step fails with
"undefined reference" to something that is plainly defined in the source
(for example `CurseForgeClient::getMods`), the build directory is holding
object files from an older version, usually because extracted files kept
timestamps older than the existing build outputs. Rebuild everything:

```sh
cmake --build build --clean-first
```

Do this after any update that changes a header, not just when the link
fails: a stale object compiled against an older struct layout can link
without error and still misbehave at runtime.

To install (the `.desktop` file is what lets the desktop associate the window
with the app; without it Qt may log a harmless-looking "Could not register app
ID" portal message on startup):

```sh
cmake --install build --prefix ~/.local
```

## Configure

Either use the GUI's Settings page, or:

```sh
./build/wam config set-key <your-curseforge-api-key>
./build/wam config set-path /path/to/WoW/_retail_
./build/wam config show
```

Config lives at `$XDG_CONFIG_HOME/wow-addon-manager/config.json`
(`~/.config/wow-addon-manager/config.json` by default). Both the key and the
path are optional: local-only operations (`list`, `remove`,
`install-manual`, `scan`, `adopt`) work with no key, though anything
touching the AddOns folder still needs `wow_path`.

The flavors CurseForge defines for WoW are cached next to it in
`flavors.json`, keyed by CurseForge's own key and carrying the display name
you see:

```json
{ "wow-forever": { "id": 12345, "name": "Forever", "api_name": "Forever" } }
```

The GUI refreshes it from CurseForge every time it starts, so the flavor list
(and the WoW folder's flavor) work offline after the first run. Edit the
names in Settings, or by hand; a name you changed is kept when the list
refreshes, and `api_name` (CurseForge's own) is what the reset button
restores. The numeric `id` is stored too, because the API filters by it.

Installed-addon state lives at
`$XDG_DATA_HOME/wow-addon-manager/installed.json`
(`~/.local/share/wow-addon-manager/installed.json` by default). Files written
by older versions (no flavor fields) load fine; those addons simply show an
unknown flavor.

## Using the GUI

A sidebar sits to the left of the page you are on and is open by default. Its
"Close Sidebar" button shrinks it to a strip of icons; it never disappears
completely. Pages are created once and kept, so switching tabs does not lose
your search text or results.

- **AddOns** (page title "Installed AddOns"): everything wam tracks, one row
  per addon, each as big as a Search result: its CurseForge icon, its name,
  and under it a line of details such as `v9.3.2 · Release · Retail · Sep 29,
  2026` (an adopted addon with no file yet reads `Adopted from your AddOns
  folder · Release · flavor not set`). On each row:
  - a **refresh button** checks that one addon for updates (the result is a
    notification, or the update popup if one is found);
  - an **arrow** opens the details. Any number of rows can be open at once, and
    they stay open while you scroll. The details are the version (with the zip's file name after it), release
    channel, flavor, release date, install time and source; the folders the
    addon owns; the changelog of the installed version, rendered as markdown on a darker
    panel (about six lines, then "... Show more" in link colour right after the
    last word shown; fetched when the details open); and
    **View on CurseForge**, which only appears once the addon is tied to a
    CurseForge file;
  - the overflow menu removes the addon (deleting its folders) or stops
    tracking it (files untouched).

  An adopted addon has no CurseForge file yet, so its details offer **Link to
  CurseForge...** instead: pick the version that matches what is installed,
  and wam records that file. Nothing is downloaded and nothing on disk
  changes. The flavor is the one you pick in that window (it starts on the
  WoW folder's), because CurseForge only lists a file under a flavor it is
  built for. Until it is known the details show a flavor box; linking, or
  updating the addon, fills it in and the box goes away. A linked addon has
  **Install another version...** instead. Details for addons recorded before
  these fields existed (version name, release date, page link) are filled in
  at startup with an API key.

  The toolbar has three buttons:
  - **Check for updates** queues every addon with a newer file and opens the
    update popup (below).
  - **Review updates (N)** appears while updates are queued, to reopen the
    popup if you closed it early.
  - **Scan for existing addons** opens the scan window (below).
- **Search**: search CurseForge; each result has an **Install** button that
  opens the install window (below).
- **Settings**: CurseForge API key, WoW folder, and under it the **flavor** of
  that folder, each with its explanation directly beneath it. The flavor is
  worked out from the folder name when the path is saved (`_retail_` is
  Retail, `_classic_era_` is Classic Era, `_ptr_` and `_beta_` count as
  Retail, and so on), matched against the cached flavors, so it works offline
  once the cache exists. If it cannot be worked out, for example for
  `_classic_`, which several flavors share, pick it by hand. The flavor box is
  greyed out until a path is set, and changing the path clears the flavor too.
  Below is the **Flavors** list: CurseForge's key on the left and an editable
  display name on the right, with a reset button on a name you changed.

Three popups:

- **Install window.** Opens from Search. The flavor starts as the one from
  Settings; changing it here affects only this install, and refetches the
  version list. Choose the release type (release, beta, alpha) and a version:
  the newest five in that channel are listed, and "Show more versions" at the
  bottom lists more, asking CurseForge for the next page only once everything
  already loaded is showing. Install is enabled as soon as a version is
  selected (the newest is pre-selected). When the chosen flavor or release type
  has no versions, the "Version" heading and list give way to a red message
  naming both. It also opens from an installed addon's "Install another
  version...", starting on that addon's flavor, and, in link mode, from "Link to
  CurseForge...". If the author has blocked third-party downloads, the
  "point wam at the zip" window follows: "Open on CurseForge" opens the direct
  download link for that exact file in your browser
  (`https://www.curseforge.com/api/v1/mods/<mod>/files/<file>/download`), and
  "Choose downloaded zip..." installs it.

- **Update review.** Diffs are reviewed **one addon at a time**: **Apply**
  installs that update and moves to the next diff, **Apply all** (to the right
  of Apply) accepts every remaining update in one go, and **Skip** drops an
  update without installing it. The popup closes itself when the queue is
  empty. Updates stay within the flavor the addon was installed for. If the
  author has disabled third-party downloads, the diff shows "Open on
  CurseForge" and "Choose zip..."; download the file in your browser, then
  point wam at it. Blocked rows are skipped by Apply all.
- **Existing addons.** Scans your AddOns folder for addons wam doesn't track
  yet. Folders whose `.toc` carries a CurseForge id are grouped by mod, shown
  with the mod's real name and icon, and adopted with one click, or all at
  once with "Adopt all matched". Folders with no tag get their own row: use
  "Search" to look the addon up on CurseForge, or type its mod id and adopt it
  by hand. Adopting only records the folders; nothing on disk changes, and the
  version stays unknown until the addon's first update. With an API key,
  adopting a mod also claims its other untracked modules (most multi-module
  addons only tag their primary one).

All network and disk work runs on a single worker thread, so the window
stays responsive during searches, downloads and updates. Requests are
processed one at a time, in order, so an Apply clicked while an update check
is still running waits behind the remaining checks.

## Working on the UI

The views are small components under `src/gui/qml/`, so a change usually
touches one file:

| File | What it is |
| --- | --- |
| `Main.qml` | window, sidebar, popups wiring |
| `InstalledPage.qml` | the AddOns page: toolbar, list, remove dialog, which rows are open |
| `AddonRow.qml` | one installed addon: header line; raises signals, owns no state |
| `AddonDetails.qml` | the expandable details (facts, folders, changelog, buttons) |
| `ChangelogBox.qml` | markdown changelog on a darker panel, with the inline "Show more" |
| `AppColors.qml` | the colour palette (page background, inset field colour, border); change colours here |
| `StyledPage.qml` | base of every page, gives them the shared background |
| `FolderChip.qml`, `AddonIcon.qml`, `FieldNote.qml` | small shared pieces |
| `SearchPage.qml`, `SettingsPage.qml`, `InstallDialog.qml`, `UpdatesDialog.qml`, `ScanDialog.qml` | the other pages and popups |

Components with knobs (colours, line counts, texts) keep them in a "Tunables"
block at the top. `AddonDetails` receives the list row as `addon`, so a new
model role is usable there as `addon.role` without further plumbing.

To see edits without rebuilding, run the GUI against the source folder:

```sh
WAM_QML_DIR=$PWD/src/gui/qml ./build/wam-gui     # fish: env WAM_QML_DIR=(pwd)/src/gui/qml ./build/wam-gui
```

It loads the QML from disk and reloads the window every time a `.qml` file in
that folder is saved (a file with a syntax error is reported on the terminal
and the window comes back once it is fixed). Without the variable the app uses
the QML compiled into the binary. New `.qml` files still need adding to
`WAM_QML_FILES` in `CMakeLists.txt` for normal builds. `src/gui/qml/qmldir`
exists only for dev mode, so that the `AppColors` singleton is found when
loading from disk; a new singleton has to be listed there too.

The palette is three fixed hex colours sampled from the Breeze Dark settings
page, so surfaces look the same on any system scheme (text and icons still
follow the system theme).

## Using the CLI

```sh
# search CurseForge
./build/wam search questie

# see which WoW flavors CurseForge currently knows about (Retail, Classic Era,
# etc.). Discovered live from the API, never hardcoded, so new Classic
# re-releases show up automatically
./build/wam flavors

# list files for a mod, optionally filtered to one flavor
./build/wam files <modId> --flavor Retail

# install the newest release file, optionally pinned to a channel/flavor
./build/wam install <modId> --channel release --flavor Retail
./build/wam install <modId> --channel beta

# if the author has blocked third-party downloads, `install` says so and
# prints the exact install-manual command to run once you have downloaded
# the file yourself from the CurseForge site
./build/wam install-manual <modId> <fileId> /path/to/downloaded.zip

# see everything the app has installed/tracked (with flavor)
./build/wam list

# remove an addon (deletes its folders + drops it from state)
./build/wam remove <modId>

# drop an addon from state WITHOUT touching its files, for re-running
# scan/adopt from a clean slate during debugging
./build/wam untrack <modId>

# update a tracked addon (shows current vs. latest, asks before applying).
# Uses the flavor the addon was installed for; --flavor overrides and re-saves it
./build/wam update <modId>
./build/wam update <modId> --yes             # skip the confirmation prompt

# update everything tracked, one diff/prompt per addon
./build/wam update-all

# find folders in AddOns/ that wam doesn't track yet, grouped by whatever
# CurseForge mod id their .toc claims (X-Curse-Project-ID)
./build/wam scan

# adopt every untracked folder tagged for a given mod, plus any sibling
# modules an API key lets us cross-check via that mod's moduleNames
./build/wam adopt --mod-id <modId>

# manually assign one folder (e.g. no X-Curse-Project-ID tag was found,
# or you know better than the tag) to a mod id
./build/wam adopt --folder <folderName> --mod-id <modId>

# record which flavor an adopted addon is for
./build/wam set-flavor <modId> "Retail"
```

`--flavor` matches case-insensitively against CurseForge's own flavor names
(`Retail`, `Classic Era`, `Burning Crusade Classic`, ...) or their slugs. An
exact name or slug match wins over a substring match, so `classic` means the
flavor called "Classic" rather than whichever "... Classic" flavor happens to
be listed first. The name is resolved via `GET /v1/games/1/version-types` and
passed through as the API's native `gameVersionTypeId` filter; it is not
string-matched against file metadata, which doesn't contain flavor names (see
"Fixed since initial testing" below). If `--flavor` matches nothing, the CLI
prints the current list of known flavor names.

## Flavors

CurseForge models a WoW flavor as a numeric `gameVersionTypeId`. wam stores
the id and the display name on each installed addon (`flavorTypeId` and
`flavorName` in `installed.json`), and shows the current name from
`flavors.json`, so renaming a flavor in Settings changes it everywhere.
Updates reuse the stored id, so an addon keeps updating within the flavor you
chose when you installed it. An adopted addon with no flavor yet is checked
in, and when updated recorded as, the WoW folder's flavor, since that is where
it lives.

Note that one `wow_path` points at one flavor folder (for example `_retail_`),
and the GUI records that folder's flavor in `config.json` (`wow_flavor_id`,
`wow_flavor_name`) as the default for installs. The install window lets you
pick another flavor for a single addon; that is stored on the addon only.
Until multi-flavor support lands, nothing stops you from installing a Classic
file into a Retail folder: the flavor chooses which CurseForge file variant to
download, it does not check it against the folder. The CLI does not use the
folder's flavor; it still takes `--flavor` per command.

## Safe installs and updates

Every install, update and manual install goes through
`AddonInstaller::installZip`, which is transactional:

1. The zip is fully extracted into `AddOns/.wam-staging` first. If extraction
   fails (corrupt zip, unsafe entry, disk full), the AddOns folder has not
   been touched.
2. Only then are the previous version's folders, plus any existing folder the
   zip would overwrite, moved into `AddOns/.wam-backup`, and the staged
   folders renamed into place. Staging, backup and AddOns share a filesystem
   (they are all inside AddOns, which also holds when AddOns is a symlink),
   so this is plain renames.
3. If anything fails during the swap, everything is rolled back to the exact
   previous state and the error is reported.
4. On success, the backup is renamed away and deleted. Renaming is the commit
   point, so a leftover `.wam-backup` always means "interrupted mid-swap".

If wam is killed mid-swap (crash, power loss), the next run restores the
backed-up folders automatically: the CLI does it before any command that
touches AddOns, and the GUI does it at startup. Modules a new version no
longer ships are removed when the swap commits. A zip with no addon folders
at all is refused rather than installed as an empty update.

Related hardening in the same pass:
- Extraction rejects absolute-path entries as well as `..` entries, and fails
  on a truncated read or write instead of quietly leaving a short file.
- `removeFolders` and `installZip` refuse folder names that are not a single
  plain path component, so a bad entry in `installed.json` cannot delete
  anything outside AddOns.
- Downloaded zips are deleted even when the install throws.
- `.wam-*` working folders are never reported by `scan`.

## What it does and doesn't do

Does:
- CurseForge search, file listing, install and update, driven entirely by the
  public CurseForge API (`api.curseforge.com/v1`), WoW gameId 1
- Discovers the Addons class id and WoW's flavor ids live from the API
  (`/v1/categories`, `/v1/games/1/version-types`) and caches them for the
  process lifetime, rather than hardcoding them. CurseForge class ids are
  global across every game on the site, so a guessed constant is a latent
  bug (see below)
- Release/beta/alpha channel selection and per-addon flavor tracking
- Detects author-blocked third-party downloads (`downloadUrl == null`) and
  falls back to a PrismLauncher-style "point me at the file" flow
- Reconciles addons that were already installed before wam existed. Each
  top-level AddOns folder is checked for a `## X-Curse-Project-ID:` line in
  its `.toc`, the id an addon's packaging tool stamps in at release time and
  the same heuristic tools like WowUp use. `scan` reports what it finds,
  grouped by mod id; `adopt` commits a match or a manual override into
  `installed.json`. This identifies *which mod*, not *which file/version*
  (there is no per-version tag), so an adopted addon is recorded with
  `fileId = 0` ("unknown") until an update pins it to a real file. Most
  multi-module addons only tag their *primary* module, so with an API key
  `adopt --mod-id` also cross-checks the mod's known files' `moduleNames`
  against what is sitting untracked on disk to claim the sibling modules.
  Without a key only self-tagged folders adopt.
- Updates with a clean swap (see "Safe installs"); an update always
  re-derives the true folder set from the freshly extracted zip, which is
  also how an adopted addon's `fileId = 0` becomes a real pinned file
- Keeps working with no API key and no network for local operations

Does not yet do:
- Choosing a specific version when updating: the install window can install
  any listed version, but updates always go to the newest file in the addon's
  release channel
- Install progress: the GUI shows an "Installing..." notice, not a progress bar
- Addon profiles, multi-flavor support (several WoW folders at once),
  GitHub/Wago sources, WoW auto-detection

Known limitations:
- No lock between processes: run one `wam` or `wam-gui` at a time. Two
  installs racing on the same AddOns folder can confuse crash recovery.
- If wam dies in the instant between a committed swap and saving
  `installed.json`, the folders are the new version while the state still
  says the old one. The next update corrects it, but an addon whose new
  version added a module would not have that module tracked until then.
- The install window sorts the versions it has loaded newest first by file
  id, but the CurseForge docs I found do not say which order the API returns
  pages in. If it returned oldest first, an addon with more than 50 files for
  one flavor would show its oldest versions first. Check with a mod that has
  many files (`GET /v1/mods/{id}/files?pageSize=5` and look at the dates).
- "Open on CurseForge" for a blocked download uses the website's own
  download endpoint, not the documented API; it is the form the site itself
  uses, but it can change without notice. The file-details backfill uses
  `POST /v1/mods/files` and the changelog `GET /v1/mods/{id}/files/{id}/changelog`,
  both found in CurseForge's SDK documentation and not yet exercised here.
- Changelogs are converted from HTML to plain text with a small converter
  (paragraphs, lists and line breaks); unusual markup may look rough.
- Addon icons are fetched from CurseForge's CDN on demand by Qt Quick's own
  image loader; they need network access and are not cached to disk by wam.

## Fixed since initial testing

Two real bugs from the first pass, both root-caused against the live API
docs rather than patched by guesswork:

1. **`search` always returned 0 results.** The code filtered by a hardcoded
   `classId=4471`, which is actually CurseForge's *Minecraft Modpacks* class.
   Class ids are global across every game on CurseForge, not per-game, so
   filtering WoW mods by a Minecraft class id matched nothing. Fixed by
   discovering the real "Addons" class id from `GET /v1/categories?gameId=1`
   at runtime instead of hardcoding a number.
2. **`--flavor` never matched, regardless of capitalization.** WoW files'
   `gameVersions` field holds real client version numbers (e.g. `"11.0.7"`),
   not flavor labels, so `"Retail"` never appears there at all. Flavor is
   actually encoded as a numeric `gameVersionTypeId`, shown on CurseForge's
   website as "Retail" / "Classic Era" / etc. but only available from the
   API. Fixed by resolving the flavor name against
   `GET /v1/games/1/version-types` and passing the numeric id straight
   through as the API's own `gameVersionTypeId` filter on
   `/v1/mods/{modId}/files`.

A third, from running the GUI:

3. **"Created graphical object was not placed in the graphics scene" on every
   tab switch.** Kirigami's `PageRow` instantiates a pushed Component or URL
   with `createObject()` and a plain `QtObject` as the parent (see
   `pagesLogic` in Kirigami's `PageRow.qml`), and QtQuick warns whenever a
   visual item is created under a non-visual parent. Reproduced with plain
   QtQuick: `createObject` with a `QtObject` parent warns, with an `Item`
   parent or none it does not. Pages now live in a `Kirigami.PagePool`, which
   creates each one once in C++ (`createWithInitialProperties`, which does not
   warn) and reuses it, so navigation no longer goes through that path.

## Layout

```
src/core/   wam_core: config, HTTP, CurseForge client, installer, state, .toc reader, reconciler
src/cli/    the wam CLI
src/gui/    Qt adapter layer (worker thread, controller, list models) and wam-gui
src/gui/qml/ Kirigami views (pages, the update and scan popups, the icon component)
data/       .desktop file
tests/      test_main.cpp (core), gui_tests.cpp (adapter layer)
```

The GUI talks to the core through `WamController` (GUI thread, what QML
sees) and `WamWorker` (one dedicated thread that owns the core's config,
state and API client). The core has no Qt dependency at all.

## Note on this environment

Built and tested in a sandboxed container (Ubuntu 24.04, GCC 13, CMake 3.28,
libzip 1.7, Qt 6.4.2) whose network egress does not include
`api.curseforge.com`. What that means for what has and hasn't been verified:

- **Verified here:** the core library and CLI build; `wam_tests` passes
  (129 checks), including zip extraction, the zip-slip and absolute-path
  guards, the install swap, rollback after a partial swap, crash recovery,
  the traversal guards, state round-trips (including a pre-flavor state file),
  flavor matching, resolving install-folder names to flavors, the flavor
  cache (merging without losing your names, hand-edited and damaged files),
  the HTML-to-text converter, the config round-trip, `.toc` reading, reconciliation and adoption, and parsing of
  CurseForge mod and file-page responses (icons, per-mod flavors, dates,
  pagination) against sample JSON.
  Several of these tests were checked to fail when the code they cover is
  removed.
- **Compiled and tested here, on Qt 6.4.2:** the GUI adapter layer (worker,
  controller, models) builds and `wam_gui_tests` passes (115 checks), including
  a run through the real worker thread that scans a temporary AddOns folder
  and adopts from it, offline, and the install window's version list (channel
  filter, five at a time, paging, de-duplication), the installed rows' texts
  and changelog state, the editable flavor list, and flavor detection from
  the cache with no network. The `wam-gui` executable was linked using a
  scratch-only workaround for Qt 6.4 (which lacks `loadFromModule`); the
  shipped code requires Qt 6.5.
- **Confirmed by real use, not by me:** an earlier version of the QML views
  was run on a desktop with Kirigami 6. That surfaced the `[undefined]` to
  QString binding errors on the update page (fixed at the source: the review
  queue's `head` always has every key, with a test that fails without the fix)
  and the graphics-scene warning above.
- **Not verified here:** the QML in this version (the PagePool navigation, the
  sidebar settings, the install, update and scan popups, the Settings flavor
  list and notes, the expandable installed rows, the blocked-download window,
  the icon component and the reworked lists) has only been linted for syntax, not rendered. The
  `PagePool`, `PagePoolAction` and drawer properties it uses were checked
  against Kirigami's upstream `master` sources, which may differ from the
  version you have installed. Nothing has been exercised against
  the live CurseForge API (`search`, `files`, `install`, `update`, adopt's
  `moduleNames` cross-check, batched mod lookups for icons, paged file lists,
  and flavor detection). In particular, icons rely on the mod's
  `logo.thumbnailUrl`, the install window's flavor list on
  `latestFilesIndexes[].gameVersionTypeId`, and version dates on the file's
  `fileDate`; if a mod reports none of these you get the placeholder icon,
  every flavor, or no date. Those
  need checking on your machine with your real key.
