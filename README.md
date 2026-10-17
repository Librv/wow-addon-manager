# wow-addon-manager

A World of Warcraft addon manager for Linux. It installs and updates addons
from CurseForge and keeps its own record of what it put where.

It comes in two forms that share one engine:

- **`wam-gui`**, a desktop app (Qt 6 and Kirigami)
- **`wam`**, a command-line tool that can do everything the app can

## Features

- Search CurseForge, install any version, and update with one click
- Release, beta and alpha channels, per addon
- Each addon remembers its WoW flavor (Retail, Classic Era, and so on)
- Finds addons you installed before wam and starts tracking them
- Installs that cannot leave an addon half-updated, even after a crash
- Works with no API key and no network for anything local

## Build

Dependencies:

| What | Arch / CachyOS | Debian / Ubuntu |
| --- | --- | --- |
| Core | `cmake`, `curl`, `nlohmann-json`, `libzip`, `openssl` | `cmake`, `libcurl4-openssl-dev`, `nlohmann-json3-dev`, `libzip-dev`, `libssl-dev` |
| GUI, to build | `qt6-base`, `qt6-declarative` (Qt 6.5 or newer) | `qt6-base-dev`, `qt6-declarative-dev` |
| GUI, to run | `kirigami`, `qqc2-desktop-style` | Kirigami and qqc2-desktop-style for Qt 6 |

Debian and Ubuntu name the runtime packages differently between releases, so
search your package manager for them.

Build and test:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

To build only the CLI, with no Qt needed, add `-DWAM_BUILD_GUI=OFF` to the
first command.

This gives you:

- `build/wam`: the CLI
- `build/wam-gui`: the app
- `build/tests/`: the test suites (offline, no API key needed)

Install to `~/.local`:

```sh
cmake --install build --prefix ~/.local
```

Installing also adds the `.desktop` file, which lets your desktop match the
window to the app. If linking fails after an update, see `docs/gotchas.md`.

## Set up

Use the app's Settings page, or the CLI:

```sh
wam config set-key        # paste your CurseForge API key; it is not shown
wam config set-path /path/to/World of Warcraft/_retail_
wam config show
```

- **API key:** needed for anything that talks to CurseForge. `set-key` reads
  it from the terminal, so it stays out of your shell history.
- **WoW path:** the flavor folder, like `_retail_`. Addons go into
  `Interface/AddOns` inside it.
- Both are optional. Listing, removing, manual installs, scanning and
  adopting all work without a key.

### Where wam keeps its files

| File | Default location | Holds |
| --- | --- | --- |
| `config.json` | `~/.config/wow-addon-manager/` | the API key and WoW path. Readable only by you. |
| `flavors.json` | next to `config.json` | the WoW flavors and the names you gave them |
| `installed.json` | `~/.local/share/wow-addon-manager/` | every addon wam tracks |

`$XDG_CONFIG_HOME` and `$XDG_DATA_HOME` move them if set. Files from older
versions of wam load fine; with an API key, the app fills in the details they
lack (authors, versions, dates) at startup.

## Using the app

The sidebar on the left switches between three pages. **Close Sidebar**
shrinks it to icons. Pages keep their state, so switching away does not lose
your search. The tabs above the page switch between [profiles](#profiles).

All network and disk work runs in the background, so the window never
freezes. Requests run one at a time, in order. An **Apply** clicked during an
update check waits until the check is done.

### AddOns

Every addon wam tracks, one row each:

- its icon, name and author
- a facts line: release date, version and flavor, for example
  `Sep 29, 2026 · v9.3.2 · Retail`. An adopted addon with no version yet shows
  `Adopted · Retail`.

On each row:

- **Refresh** checks that addon for updates.
- **The arrow** opens its details. Open as many as you like; they stay open
  while you scroll.
- **The menu** has **Remove…** (deletes its folders) and **Stop tracking
  (keep files)**.

The details show:

- version and zip name, release channel, flavor, release date, install time
  and source
- the folders the addon owns
- the changelog of the installed version, with **… Show more** for long ones
- **View on CurseForge**, once the addon is linked to a CurseForge file
- **Install another version…**

An adopted addon has **Link to CurseForge…** instead of the last two:

- Pick the version you have installed, and wam records it. Nothing is
  downloaded or changed on disk.
- The window starts on the WoW folder's flavor; change it if the addon is for
  another one.
- Until the flavor is known, the details show a flavor box. Linking or
  updating fills it in.

The toolbar:

- **Check for updates** checks every addon and opens the update review.
- **Review updates (N)** reopens the review if you closed it early.
- **Scan for existing addons** finds addons wam does not track yet. Only
  for the active profile, since it reads the game folder.
- **Set active profile** shows on a profile that is not active (see
  [Profiles](#profiles)).

### Profiles

A profile is a named set of addons for one WoW folder, like a modpack. The
first one is made for you, named after your flavor (or `Default`).

- **Tabs** at the top show one profile each. The one with a green dot is
  **active**: its addons are the ones in the game's AddOns folder. Long
  names are cut off; hover a tab to see the full name.
- **New profile…** (the `+` button) makes an empty profile with the same
  folder and flavor as the current one.
- **Rename…** and **Delete…** are on a right click on a tab, or the menu
  button at the end of the tabs. The active profile cannot be deleted.
- A profile that is not active can still be edited: install, update and
  remove addons as usual. Those changes are saved and applied when you click
  **Set active profile** on the AddOns page.
- Switching only installs and removes what differs, from zips wam keeps, so
  nothing is downloaded again.
- Addon settings (SavedVariables in `WTF/`) are shared by all profiles.

### Search

Search CurseForge. Each result shows its name, author, summary, download
count, latest release date and version. **Install** opens the install window.

### Settings

- **CurseForge API key**, shared by all profiles.
- **WoW folder**, in the section named after the current profile (for
  example **Profile: Retail**): each profile has its own folder and flavor.
  Under it is the **flavor**, which is worked out from the
  folder name: `_retail_` is Retail, `_classic_era_` is Classic Era, and
  `_ptr_` and `_beta_` count as Retail.
  - If it cannot tell (for example `_classic_`, which several flavors share),
    pick it by hand.
  - Changing the folder clears the flavor. An active profile stays active on
    the new folder unless another profile is active there.
- **Flavors**: every WoW flavor CurseForge knows, with a name you can edit.
  The reset button brings back CurseForge's name.

### Install window

Opens from **Install** in Search, or **Install another version…** on an
addon.

- Pick the flavor, release type and version. The newest version is selected
  for you.
- The flavor starts as the one in Settings. Changing it here affects only
  this install.
- Five versions show at first; **Show more versions** lists more.
- If there are no versions for that flavor and release type, the window says
  so.

### Update review

Shows one update at a time:

- **Apply** installs it and moves to the next.
- **Apply all (N)** installs every remaining update.
- **Skip** drops it without installing.

Updates stay within the flavor the addon was installed for. The window closes
when nothing is left.

### Blocked downloads

Some authors block downloads by other apps. When that happens:

1. **Open on CurseForge** opens the download for that exact file in your
   browser.
2. **Choose downloaded zip…** (or **Choose zip…** in the update review)
   installs the file you downloaded.

**Apply all** skips blocked updates.

### Existing addons

**Scan for existing addons** looks for addons in your AddOns folder that wam
does not track yet.

- Addons whose files name their CurseForge project are matched and shown with
  their real name and icon. Adopt one, or all with **Adopt all matched (N)**.
- Any other folder gets its own row. Use **Search** to find it on CurseForge,
  or type its mod id and adopt it.

Adopting only records the folders; nothing on disk changes. The version stays
unknown until the first update. With an API key, adopting an addon also picks
up its other folders (most multi-folder addons only tag one).

## Using the CLI

```sh
wam search questie                        # search CurseForge
wam flavors                               # list WoW flavors
wam files <modId> --flavor Retail         # list a mod's files
wam install <modId>                       # newest release
wam install <modId> --channel beta --flavor "Classic Era"
wam install-manual <modId> <fileId> addon.zip   # for blocked downloads
wam list                                  # everything wam tracks
wam update <modId>                        # shows the change, asks first
wam update <modId> --yes                  # no question
wam update-all                            # one question per addon
wam remove <modId>                        # delete its folders, stop tracking
wam untrack <modId>                       # stop tracking, keep the files
wam scan                                  # find addons wam does not track
wam adopt --mod-id <modId>                # track every folder of that mod
wam adopt --folder <name> --mod-id <modId>  # track one folder by hand
wam set-flavor <modId> Retail             # set an adopted addon's flavor
```

Profiles (see [Profiles](#profiles)):

```sh
wam config set-path ~/Games/wow/_retail_  # the selected profile's WoW folder
wam config set-flavor Retail              # if the folder name did not say
wam profile list                          # * = selected, (active) = in AddOns
wam profile create Raid                   # empty, same folder and flavor
wam profile select Raid                   # later commands act on Raid
wam --profile Raid install <modId>        # one command on another profile
wam profile activate Raid                 # put Raid's addons in AddOns
wam profile rename Raid Mythic
wam profile delete Mythic                 # not the active one
```

Notes:

- **Blocked downloads:** `install` says so and prints the exact
  `install-manual` command to run once you have the zip.
- **`--flavor`** ignores case and matches CurseForge's names or slugs. An
  exact match wins, so `classic` means the flavor called "Classic". If
  nothing matches, `wam` lists the flavors it knows.
- **Updates** use the flavor the addon was installed for. `--flavor` on
  `update` changes it.
- **Profiles:** every command acts on the selected profile, with its folder
  and flavor. `--profile <name-or-id>` picks another one for one command.
- **A profile that is not active** can be changed with `install`,
  `install-manual`, `update` and `remove`; the game folder changes when you
  run `wam profile activate`. `scan` and `adopt` need the active profile.

## Flavors

A flavor is a version of WoW: Retail, Classic Era, and so on.

- Each addon keeps the flavor it was installed for, and updates stay within
  it.
- Renaming a flavor in Settings renames it everywhere.
- An adopted addon with no flavor uses the WoW folder's flavor.
- The app uses the WoW folder's flavor for new installs. The CLI does not; it
  takes `--flavor` per command.
- wam does not check that a file matches your folder. Installing a Classic
  file into a Retail folder is possible.

## Safe installs

Every install and update works like this:

1. The zip is unpacked into a staging folder. If that fails, your AddOns
   folder is untouched.
2. The old version is moved to a backup folder and the new one moved in.
3. If anything fails, the old version is put back exactly as it was.
4. Only then is the backup deleted.

If wam is killed in the middle (a crash, power loss), the next run restores
the old version on its own. Also:

- Folders a new version no longer ships are removed.
- A zip with no addon in it is refused.
- A zip cannot write outside your AddOns folder.

## Not yet supported

- Updating to a specific version (updates always take the newest; use
  **Install another version…** instead)
- A progress bar for installs
- Addon profiles, several WoW folders at once, GitHub or Wago as sources,
  finding your WoW folder automatically

## Known limitations

- Run one `wam` or `wam-gui` at a time. Two installs into the same AddOns
  folder at once can confuse crash recovery.
- If wam dies right after swapping in a new version but before saving its
  records, it still lists the old version until the next update.
- Some CurseForge requests (changelogs, filling in file details, the blocked
  download link) are not yet confirmed against the live API. See
  `docs/curseforge-api.md`.
- Changelogs are converted to plain text simply; unusual formatting may look
  rough.
- Icons load from CurseForge each time and need network access.

## Contributing

Developer docs are in `AGENTS.md` (rules for people and AI agents) and
`docs/`: architecture, GUI development, the CurseForge API, testing, gotchas
and design decisions.

Once per clone, turn on the check that refuses commits containing your API
key:

```sh
git config core.hooksPath tools/git-hooks
```
