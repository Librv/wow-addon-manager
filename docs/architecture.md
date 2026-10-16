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
- **`WamWorker`** runs on one dedicated thread and owns the core's `Config`,
  `StateStore` and `CurseForgeClient`. Requests are processed one at a time,
  in the order they were sent.
- **`WamController`** lives on the GUI thread and is the only object QML talks
  to. Its slots forward to the worker with queued calls and return at once;
  results come back as signals or model updates. It owns the list models
  (`InstalledAddonsModel`, `SearchResultsModel`, `PendingUpdatesModel`,
  `ScanResultsModel`, `InstallFilesModel`, `FlavorsModel`).

## State on disk

| File | Location | Holds |
| --- | --- | --- |
| `config.json` | `$XDG_CONFIG_HOME/wow-addon-manager/` | API key, `wow_path`, the folder's flavor (`wow_flavor_id`, `wow_flavor_name`). Mode `0600`, folder `0700` (`docs/decisions/0007-keep-the-api-key-private.md`) |
| `flavors.json` | next to `config.json` | CurseForge flavors keyed by CurseForge's key: `id`, user-editable `name`, original `api_name` |
| `installed.json` | `$XDG_DATA_HOME/wow-addon-manager/` | one `InstalledAddon` per tracked addon (see `src/core/state_store.hpp`) |

Every field added later has an "unknown" default, so files from older
versions load. Notable unknowns: `fileId == 0` (adopted, no file yet),
`flavorTypeId == 0` (flavor unknown), empty `author` / `iconUrl` /
`fileDate` (backfilled by the GUI at startup when there is an API key).

`config.json` and `installed.json` are written atomically (`writeFileAtomic`:
temp file, sync, rename), so a crash leaves the old file or the new one. An
`installed.json` that still cannot be read is moved aside as
`installed.json.damaged-<time>` and the list starts empty; the CLI warns and
the GUI reports it, so the next save cannot overwrite what the old file held.

## Flavors

CurseForge models a WoW flavor as a numeric `gameVersionTypeId`. The list is
fetched from `GET /v1/games/1/version-types`, cached in `flavors.json`, and
refreshed every time the GUI starts. User renames survive a refresh.
Each addon stores the id and name it was installed for, and updates stay
within that id. The WoW folder's flavor is worked out from the folder name
(`_retail_`, `_classic_era_`, ...) against the cache, so it works offline.

## Installs and updates

Every install, update and manual install goes through
`AddonInstaller::installZip`:

1. Extract the whole zip into `AddOns/.wam-staging`. On failure, `AddOns/` is
   untouched.
2. Move the previous version's folders, and any folder the zip would
   overwrite, into `AddOns/.wam-backup`; rename the staged folders into place.
   All three live inside `AddOns/`, so these are plain renames on one
   filesystem.
3. On any failure, roll back to the exact previous state.
4. On success, rename the backup away and delete it. That rename is the
   commit point.

A leftover `.wam-backup` means an interrupted swap. The CLI restores it before
any command that touches `AddOns/`; the GUI does it at startup. An update
re-derives the folder set from the new zip, which is also how an adopted
addon gets a real `fileId`.

Known gaps: no lock between processes (run one `wam` or `wam-gui` at a time),
and a crash between the committed swap and saving `installed.json` leaves the
state one version behind until the next update.

## Reconciling existing addons

`scan` reads `## X-Curse-Project-ID:` from each untracked folder's `.toc` and
groups folders by mod id. `adopt` records them with `fileId = 0` and
`adopted = true`; nothing on disk changes. Most multi-module addons only tag
their primary module, so with an API key `adopt --mod-id` also matches the
mod's files' `moduleNames` against untracked folders.
