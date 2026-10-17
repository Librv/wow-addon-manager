# RAM testing

`tools/ram-test.sh` starts the real `wam-gui`, lets it idle until its memory
settles, and reports RSS, PSS, anonymous, file and shared memory. Use it to
compare two versions of the code, not for absolute numbers.

## Usage

Build the app first (`cmake --build build -j`). The script runs `build/wam-gui`
on your system Qt and Kirigami.

| Command | What it does |
| --- | --- |
| `tools/ram-test.sh` | empty state, 3 runs, offscreen |
| `tools/ram-test.sh --seed 100` | 100 fake tracked addons (40 without a number) |
| `tools/ram-test.sh --qml src/gui/qml` | load QML from a directory, no rebuild needed |
| `tools/ram-test.sh --bin PATH` | measure another `wam-gui` (or set `WAM_GUI_BIN`) |
| `tools/ram-test.sh --wayland` | run on your real Wayland session |
| `tools/ram-test.sh --detail 10` | list the top 10 mappings by PSS after the first run |
| `tools/ram-test.sh --software` | `QT_QUICK_BACKEND=software` |
| `tools/ram-test.sh --runs N` | launches to measure (default 3) |
| `tools/ram-test.sh --wait SEC` | longest idle before reading (default 15) |

Work files go to `~/.cache/wam-ramtest` (override with `WAM_RAMTEST_DIR`).

## How a run works

- HOME, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_RUNTIME_DIR` point into the
  work dir, so your real `~/.config/wow-addon-manager` is never touched.
- Each run polls PSS every 0.5 s and reads memory once it moves less than
  0.3 MB for 4 samples in a row. The `secs` column shows how long that took.
- Offscreen is the default so A/B runs do not depend on your compositor.
- `--wayland` needs the real Wayland socket. It is untested inside a sandbox
  that blocks that socket.

## Reading the results

- Compare PSS and anonymous memory. RSS counts shared libraries in full.
- Compare only runs from the same setup and platform. Wayland and the GPU add
  memory that offscreen does not have.
- With no API key the app makes no CurseForge requests, so search, icons and
  update checks are not measured. `--seed` only fills the installed list.

## Findings

Offscreen, Qt 6.12, Kirigami 6.31, `org.kde.desktop` style, measured
2026-10-17.

**Result of the fixes.** Idle PSS, before and after:

| Addons | Before | After |
| --- | --- | --- |
| 1 | ~54 MB | ~43 MB |
| 20 | ~87 MB | ~49 MB |

What each fix saved, at 20 addons:

| Fix | Saved |
| --- | --- |
| Row highlight only on hover (`ListRowBackground`) | ~15 MB |
| `AddonDetails` only while the row is open | ~10 MB |
| Row menu on first click | ~3 MB |
| Dialogs on first use | ~6 to 8 MB (removing them all outright: ~13 MB) |

**Tried, no gain:**

- glibc `arena_max=1` or `2`, before the fixes
- one `malloc_trim(0)` a few seconds after startup, after the fixes

A lower glibc `trim_threshold` saved ~9 MB before the fixes. Since a full
trim frees nothing after them, that gain came from the heavy rows.

The rest of this section is from before the fixes.

**Memory levels off at about 15 to 25 addons.** Rows far from the viewport are
cheap placeholders, rows near it are fully built.

| Addons | PSS |
| --- | --- |
| 0 | ~53 MB |
| 1 | ~54 MB |
| 15 | ~85 MB |
| 20 | ~86 to 87 MB |
| 400 | ~92 MB |

**A loaded installed row cost about 2.2 MB:**

- the desktop style's row background, ~1 MB (two StyleItems, each painting a
  full-row image)
- `AddonDetails` built while collapsed, ~0.6 MB
- the per-row menu, ~0.2 MB
- tool buttons, ~70 KB
- the rest is plain QML objects

**The style matters.** At 20 addons:

| Style | PSS |
| --- | --- |
| `org.kde.desktop` | ~87 MB |
| Fusion | ~64 MB |
| Basic | ~61 MB |

## Sandbox mode

`--sandbox` is the original setup, for a machine without Qt 6.5 or Kirigami 6.
It installs packages with `apt-get`, so use it only on Ubuntu 24.04.

```sh
tools/ram-test.sh --sandbox                  # empty state, 3 runs
tools/ram-test.sh --sandbox --seed 100       # 100 fake tracked addons
tools/ram-test.sh --sandbox --setup-only     # just install deps and build Kirigami
```

It was written for a 1-core Ubuntu 24.04 sandbox with about 3 GB of RAM,
Qt 6.4.2, no Kirigami 6 or `qqc2-desktop-style` packages, no GPU or Wayland,
and no access to `api.curseforge.com`. So it:

- builds Kirigami 6.0.0 from the KDE GitHub mirrors, with small Qt 6.4 patches
- compiles the repo's sources in a scratch project, changing only `main.cpp`
  (Qt 6.4 lacks `loadFromModule`)
- loads the repo's QML unmodified through `WAM_QML_DIR`
- runs on Qt's `offscreen` platform
- idles a fixed `--wait` time (default 8 s) instead of waiting for PSS to settle

The first run builds Kirigami, which is slow on one core. Kirigami script
warnings on stderr are expected on Qt 6.4. On a real desktop, Wayland and the
GPU add roughly 40 MB PSS that this setup does not have.

Reference points from the sandbox setup, idle with an empty state:

| What | PSS | Other |
| --- | --- | --- |
| the sandbox script | ~70 MB | 22 MB anon |
| bare `QApplication` | 18 MB | |
| minimal Qt Quick window | 50 MB | |
| minimal Widgets window | 26 MB | |
| the app on the author's machine | ~90 MB | 200 MB RSS |
| the same, with `QT_QPA_PLATFORM=offscreen` | ~53 MB | 117 MB RSS |
