# wow-addon-manager — milestone 2

Milestone 1's core engine + CurseForge source + manual folder-pointing for
blocked downloads, plus milestone 2's reconciliation of addons that were
already sitting in `AddOns/` before `wam` existed. No UI yet — everything is
verified through the `wam` CLI.

## Build

Dependencies (Ubuntu/Debian package names — CachyOS/Arch equivalents in
parentheses): `cmake`, `libcurl4-openssl-dev` (`curl`), `nlohmann-json3-dev`
(`nlohmann-json`), `libzip-dev` (`libzip`), `libssl-dev` (`openssl`).

```sh
cmake -S . -B build
cmake --build build -j
```

This produces two binaries:
- `build/wam` — the CLI
- `build/tests/wam_tests` — offline self-tests (no network/API key needed)

Run the self-tests any time:
```sh
./build/tests/wam_tests
```

## Configure

```sh
./build/wam config set-key <your-curseforge-api-key>
./build/wam config set-path /path/to/WoW/_retail_
./build/wam config show
```

Config lives at `$XDG_CONFIG_HOME/wow-addon-manager/config.json`
(`~/.config/wow-addon-manager/config.json` by default). Both the key and
the path are optional — the app stays usable for local-only operations
(`list`, `remove`, `install-manual`) with neither set, except that
anything touching the AddOns folder still needs `wow_path`.

Installed-addon state lives at
`$XDG_DATA_HOME/wow-addon-manager/installed.json`
(`~/.local/share/wow-addon-manager/installed.json` by default).

## Use

```sh
# search CurseForge
./build/wam search questie

# see which WoW flavors CurseForge currently knows about (Retail, Classic Era, etc.)
# — discovered live from the API, never hardcoded, so new Classic re-releases
# show up automatically
./build/wam flavors

# list files for a mod, optionally filtered to one flavor
./build/wam files <modId> --flavor Retail

# install the newest release file, optionally pinned to a channel/flavor
./build/wam install <modId> --channel release --flavor Retail
./build/wam install <modId> --channel beta

# if the author has blocked third-party downloads, `install` will tell you
# and print the exact install-manual command to run once you've downloaded
# the file yourself from the CurseForge site
./build/wam install-manual <modId> <fileId> /path/to/downloaded.zip

# see everything the app has installed/tracked
./build/wam list

# remove an addon (deletes its folders + drops it from state)
./build/wam remove <modId>

# drop an addon from state WITHOUT touching its files — for re-running
# scan/adopt from a clean slate during debugging
./build/wam untrack <modId>

# update a tracked addon (shows current vs. latest, asks before applying)
./build/wam update <modId> --flavor Retail
./build/wam update <modId> --yes             # skip the confirmation prompt

# update everything tracked, one diff/prompt per addon
./build/wam update-all --flavor Retail

# find folders in AddOns/ that wam doesn't track yet, grouped by whatever
# CurseForge mod id their .toc claims (X-Curse-Project-ID)
./build/wam scan

# adopt every untracked folder tagged for a given mod, plus any sibling
# modules an API key lets us cross-check via that mod's moduleNames
./build/wam adopt --mod-id <modId>

# manually assign one folder (e.g. no X-Curse-Project-ID tag was found,
# or you know better than the tag) to a mod id
./build/wam adopt --folder <folderName> --mod-id <modId>
```

`--flavor` matches case-insensitively against CurseForge's own flavor names
(`Retail`, `Classic Era`, `Burning Crusade Classic`, ...) or their slugs, and
is resolved via `GET /v1/games/1/version-types` and passed straight through
as the API's native `gameVersionTypeId` filter — not string-matched against
file metadata, which doesn't actually contain flavor names (see "Fixed since
initial testing" below). If `--flavor` doesn't match anything, the CLI
prints the current list of known flavor names.

## What this milestone does and doesn't do

Does:
- CurseForge search / file listing / install, driven entirely by the public
  CurseForge API (`api.curseforge.com/v1`), WoW gameId 1
- The Addons class id and WoW's flavor/version-type ids are **discovered
  live** from the API (`/v1/categories`, `/v1/games/1/version-types`) and
  cached for the process lifetime, rather than hardcoded — CurseForge class
  ids are global across every game on the site, so a guessed constant is a
  latent bug waiting to happen (see below)
- Release/beta/alpha channel selection and flavor filtering via the API's
  own `gameVersionTypeId` parameter
- Detects author-blocked third-party downloads (`downloadUrl == null`) and
  falls into the PrismLauncher-style manual pointing flow instead of failing
- Zip extraction is zip-slip-safe and reports back exactly which top-level
  AddOns folders a given install owns, for later removal/updates
- Local state (`installed.json`) is independent of the game folder itself
- **Reconciling addons that were already installed before `wam` existed.**
  Each top-level `AddOns/` folder is checked against its own `.toc` file(s)
  for a `## X-Curse-Project-ID:` line — the id an addon's packaging tool
  (e.g. the BigWigsMods packager) stamps in at release time, the same
  heuristic tools like WowUp use for this. `scan` reports what it finds,
  grouped by mod id (a mod can own several top-level folders); `adopt`
  commits a match — or a manual override — into `installed.json`. Note this
  identifies *which mod*, not *which exact file/version*: there's no
  per-version tag in the `.toc`, so an adopted addon is recorded with
  `fileId = 0` ("unknown") until `wam update` pins it to a real CurseForge
  file. Also note that in practice, most multi-module addons only stamp the
  tag into their *primary* module's `.toc` — the rest ship untagged. Because
  of this, `adopt --mod-id <id>` doesn't just union self-tagged folders: if
  an API key is configured, it additionally cross-checks the mod's known
  files' `moduleNames` against whatever's sitting untracked on disk, so
  sibling modules (ActionBars, Bags, etc.) get claimed too. With no API key,
  only self-tagged folders adopt, and `wam update <modId>` afterward will
  still pick up the rest on its next real install. A folder with no tag at
  all and no matching sibling (hand-written addons, or ones from a source
  other than CurseForge) needs `--folder ... --mod-id ...` to assign by
  hand.
- **Updating tracked addons.** `wam update <modId>` fetches the current
  best file for that mod's tracked channel/flavor, shows current-vs-latest,
  and asks for confirmation before downloading and re-extracting (`--yes`
  skips the prompt, for scripting). `wam update-all` does the same for
  every CurseForge-sourced tracked addon in one pass. An update does a
  clean swap — old folders are removed before the new file's folders are
  extracted, so a module the new release dropped doesn't linger — and
  always re-derives the true folder set from the freshly-extracted zip
  regardless of what was tracked before, which is also how this recovers
  an adopted addon's `fileId = 0` into a real pinned file.

Does not yet do (later phases per the agreed plan):
- Any UI (phase 3) — this is CLI-only by design for this milestone
- Addon profiles (phase 4), multi-flavor support (phase 5), GitHub/Wago
  sources (phase 6), or WoW auto-detection (phase 7)

## Fixed since initial testing

Two real bugs from the first pass, both root-caused against the live API
docs rather than patched by guesswork:

1. **`search` always returned 0 results.** The code filtered by a hardcoded
   `classId=4471`, which is actually CurseForge's *Minecraft Modpacks* class
   — class ids are global across every game on CurseForge, not per-game, so
   filtering WoW mods by a Minecraft class id matched nothing. Fixed by
   discovering the real "Addons" class id from `GET /v1/categories?gameId=1`
   at runtime instead of hardcoding a number.
2. **`--flavor` never matched, regardless of capitalization.** WoW files'
   `gameVersions` field holds real client version numbers (e.g. `"11.0.7"`),
   not flavor labels — `"Retail"` never appears there at all, so no amount
   of case-fixing would have helped. Flavor is actually encoded as a numeric
   `gameVersionTypeId`, shown on CurseForge's website as "Retail" / "Classic
   Era" / etc. but only available from the API. Fixed by resolving the
   flavor name against `GET /v1/games/1/version-types` (case-insensitive)
   and passing the numeric id straight through as the API's own
   `gameVersionTypeId` filter on `/v1/mods/{modId}/files`.



## Note on this environment

This was built and tested in a sandboxed container whose network egress
does not include `api.curseforge.com`, so the CurseForge calls (`search`,
`files`, `install`, `update`, `update-all`, and `adopt`'s moduleNames
cross-check) are implemented and unit-tested for parsing/selection logic,
but not exercised against the live API from here — that verification
needs to happen on your machine with your real key. `install-manual`,
`list`, `remove`, `scan`, `adopt`'s self-tagged (offline) path, zip
extraction (including the zip-slip guard), and the state store are fully
exercised end-to-end (see "Build" above and `tests/test_main.cpp`).
