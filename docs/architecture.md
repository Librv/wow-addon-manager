# Architecture

## Layout

```
src/core/     wam_core: config, HTTP, CurseForge client, installer, state, .toc reader, reconciler, flavor cache, HTML-to-text
src/cli/      the wam CLI (main.cpp)
src/gui/      Qt adapter layer (worker thread, controller, list models) and wam-gui's main.cpp
src/gui/qml/  Kirigami views: pages, popups and shared components (see docs/gui.md)
tests/        test_main.cpp (core), flavor_names_test.cpp (flavor naming), gui_tests.cpp (adapter)
tools/        ram-test.sh (see docs/ram-testing.md)
data/         .desktop file
```

## Layers

```
 wam (CLI) ───────────────┐
                          ├──► wam_core ──► CurseForge API, AddOns/ folder, state files
 QML ─► WamController ─► WamWorker ┘
        (GUI thread)     (one worker thread)
```

- **`wam_core`** has no Qt dependency. Its calls block on network and disk.
- **`wam`** calls the core directly; blocking is fine in a CLI.
- **`WamWorker`** runs on one dedicated thread and owns the core's `Config`
  (only the API key is used), `ProfileStore`, the selected profile's
  `StateStore` and the `CurseForgeClient`. Requests are processed one at a
  time, in the order they were sent. Everything it does acts on the selected
  profile.
- **`WamController`** lives on the GUI thread and is the only object QML talks
  to. Its slots forward to the worker with queued calls and return at once;
  results come back as signals or model updates. It owns the list models
  (`InstalledAddonsModel`, `SearchResultsModel`, `PendingUpdatesModel`,
  `ScanResultsModel`, `InstallFilesModel`, `FlavorsModel`).

## State on disk

| File | Location | Holds |
| --- | --- | --- |
| `config.json` | `$XDG_CONFIG_HOME/wow-addon-manager/` | API key, `wow_path`, the folder's flavor (`wow_flavor_id`, `wow_flavor_name`). The GUI reads the folder and flavor only to make the first profile. Mode `0600`, folder `0700` (`docs/decisions/0007-keep-the-api-key-private.md`) |
| `flavors.json` | next to `config.json` | CurseForge flavors keyed by CurseForge's key: `id`, user-editable `name`, original `api_name` |
| `installed.json` | `$XDG_DATA_HOME/wow-addon-manager/` | the addon list from before profiles. Copied into the first profile, then left in place |
| `profiles.json` | next to `installed.json` | the profiles (`id`, `name`, `autoNamed`, `wowPath`, `flavorId`, `flavorName`, `active`), the `selected` one and `nextId` |
| `profiles/<id>/installed.json` | next to `installed.json` | one `InstalledAddon` per tracked addon in that profile (see `src/core/state_store.hpp`) |
| `profiles/switch.json` | next to `installed.json` | the plan and progress of a profile switch; only there while one runs |
| `profiles/switch-backup/<folder>.zip` | next to `installed.json` | copies of untracked folders a switch overwrites; deleted when it commits or is rolled back |
| `library/<modId>/<fileId>.zip` | next to `installed.json` | addon zips shared by all profiles; `adopted-<profileId>.zip` for snapshots of adopted addons |

Every field added later has an "unknown" default, so files from older
versions load. The notable ones:

- `fileId == 0`: adopted, no file yet
- `flavorTypeId == 0`: flavor unknown
- empty `author`, `iconUrl`, `fileDate`: filled in by the GUI at startup when
  there is an API key

Writes and damage:

- `config.json`, `profiles.json` and each `installed.json` are written atomically
  (`writeFileAtomic`: temp file, sync, rename). A crash leaves the old file or
  the new one.
- An `installed.json` that cannot be read is moved aside as
  `installed.json.damaged-<time>`, and the list starts empty. The CLI warns,
  the GUI reports it, and the next save cannot overwrite the old data.
- A damaged `profiles.json` is moved aside the same way. Each
  `profiles/<id>/` folder on disk comes back as an inactive profile named
  `Recovered <id>` with no folder. Only with none does it migrate from
  `config.json` and `installed.json` again, since those stop being updated
  once profiles exist.
- Other profiles' `installed.json` files are only read with
  `StateStore::peek`, which never renames. A damaged one is left alone and
  library cleanup is skipped until it is fixed.

## Profiles

A profile is a named set of addons for one WoW folder and flavor
(`src/core/profiles.hpp`, `docs/decisions/0008-profiles-share-a-zip-library.md`).

- **One active profile per WoW folder.** Its addons are what is in `AddOns/`.
  Inactive profiles are edited in the library only.
- **The library** keeps every zip a profile uses, so switching back never
  downloads again. `collectGarbage` deletes zips no profile folder and no
  pending `switch.json` refers to.
- **Switching** (`ProfileOps::activate`) diffs the two profiles:
  1. Snapshot old addons with no library zip (from before profiles) into the
     library, and adopted addons again every time (they may have been edited).
  2. Make sure every zip to install is there and fully readable (every
     entry, so corrupt data shows up). A missing or corrupt zip is fetched
     again; if that is not possible, fail before `AddOns/` changes.
  3. Copy untracked folders a zip will overwrite into
     `profiles/switch-backup/`, then write `profiles/switch.json`. It keeps
     the folder, and records progress as steps start.
  4. Remove old-only addons, install new and changed ones with `installZip`,
     and leave addons at the same mod and file id alone.
  5. Flip the active flags, save, delete `switch.json`.
- **An interrupted switch** (a crash) leaves `switch.json`.
  `ProfileOps::recoverSwitch` reruns steps 4 and 5 (each is safe to repeat).
  Front ends call it at startup, after `recoverInterrupted`. A profile in an
  unfinished switch cannot be removed.
- **A switch that fails** in step 4 is rolled back, touching only what it
  got to: folders from started steps are removed, backed-up untracked
  folders they overwrote are restored, and only old addons the switch
  touched are reinstalled from the library (the others keep local edits).
  Then `switch.json` and the backup are deleted and the error is reported.
  The old profile stays active. Only if the rollback fails too does
  `switch.json` stay.
- **A plan whose target is already active** (a crash between saving the
  flags and deleting the plan) is only deleted. A plan for a profile with no
  folder gives it the plan's folder when it finishes.
- **Changing a profile's folder** keeps an active profile active when no
  other profile is active on the new folder (a moved game folder). Folders
  are compared after resolving symlinks.
- **First run:** with no `profiles.json`, one profile is made from
  `config.json`'s folder and flavor and a copy of `installed.json`. It is named
  after the flavor ("Default" if unknown), and the name follows the flavor
  until the user renames it.

## Flavors

CurseForge models a WoW flavor as a numeric `gameVersionTypeId`.

- The list comes from `GET /v1/games/1/version-types`, is cached in
  `flavors.json`, and is refreshed every time the GUI starts. User renames
  survive a refresh.
- Each addon stores the id and name it was installed for. Updates stay within
  that id.
- The WoW folder's flavor is worked out from the folder name (`_retail_`,
  `_classic_era_`, ...) against the cache, so it works offline.

## Installs and updates

Every install, update and manual install goes through
`AddonInstaller::installZip`. For addons in a profile, front ends call
`ProfileOps` (`placeZip`, `removeAddon`, `activate`), which stores the zip in
the library and calls `installZip` only when the profile is active:

1. Extract the whole zip into `AddOns/.wam-staging`. On failure, `AddOns/` is
   untouched.
2. Move the previous version's folders, and any folder the zip would
   overwrite, into `AddOns/.wam-backup`; rename the staged folders into place.
   All three live inside `AddOns/`, so these are plain renames on one
   filesystem.
3. On any failure, roll back to the exact previous state.
4. On success, rename the backup away and delete it. That rename is the
   commit point.

- A leftover `.wam-backup` means an interrupted swap. The CLI restores it
  before any command that touches `AddOns/`; the GUI does it at startup, for
  every active profile's folder, then finishes an interrupted switch.
- An update takes its folder set from the new zip. That is also how an
  adopted addon gets a real `fileId`.

Known gaps:

- No lock between processes: run one `wam` or `wam-gui` at a time.
- A crash between the committed swap and saving `installed.json` leaves the
  state one version behind until the next update.

## Reconciling existing addons

- `scan` reads `## X-Curse-Project-ID:` from each untracked folder's `.toc`
  and groups folders by mod id.
- `adopt` records them with `fileId = 0` and `adopted = true`. Nothing on disk
  changes.
- Most multi-module addons tag only their primary module. With an API key,
  `adopt --mod-id` also matches the mod's files' `moduleNames` against
  untracked folders.
