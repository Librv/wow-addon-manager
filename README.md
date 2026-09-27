# wow-addon-manager — milestone 1

Core engine + CurseForge source + manual folder-pointing for blocked
downloads. No UI yet — everything is verified through the `wam` CLI.

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

Does not yet do (later phases per the agreed plan):
- Reconciling already-installed addons via fingerprint matching (phase 2)
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
`files`, `install`) are implemented and unit-tested for parsing/selection
logic, but not exercised against the live API from here — that verification
needs to happen on your machine with your real key. `install-manual`,
`list`, `remove`, zip extraction (including the zip-slip guard), and the
state store are fully exercised end-to-end (see "Build" above and
`tests/test_main.cpp`).
