# RAM testing

`tools/ram-test.sh` starts the real `wam-gui` (the repo's QML, real Kirigami),
lets it idle, and reports RSS, PSS and anonymous memory. Use it to compare two
versions of the code, not for absolute numbers.

```sh
tools/ram-test.sh                 # empty state, 3 runs
tools/ram-test.sh --seed 100      # 100 fake tracked addons
tools/ram-test.sh --software      # QT_QUICK_BACKEND=software
tools/ram-test.sh --setup-only    # just install deps and build Kirigami
```

Work files go to `~/.cache/wam-ramtest` (override with `WAM_RAMTEST_DIR`). The
first run builds Kirigami, which is slow on one core; later runs only rebuild
wam-gui.

## Why it looks the way it does

It was written for an Ubuntu 24.04 sandbox with 1 core, about 3 GB of RAM,
Qt 6.4.2, no Kirigami 6 or `qqc2-desktop-style` packages, no GPU or Wayland,
and no access to `api.curseforge.com`. So the script:

- builds Kirigami 6.0.0 from the KDE GitHub mirrors, with small Qt 6.4 patches
- compiles the repo's sources in a scratch project, changing only `main.cpp`
  (Qt 6.4 lacks `loadFromModule`)
- loads the repo's QML unmodified through `WAM_QML_DIR`
- runs on Qt's `offscreen` platform

Kirigami script warnings on stderr are expected on Qt 6.4.

## Reading the results

- Compare PSS and anonymous memory. RSS counts shared libraries in full.
- Compare only runs from the same setup. On a real desktop, Wayland and the GPU
  add roughly 40 MB PSS that this setup does not have.
- With no API key the app makes no CurseForge requests, so search, icons and
  update checks are not measured. `--seed` only fills the installed list.

Reference points, idle with an empty state:

| What | PSS | Other |
| --- | --- | --- |
| this script | ~70 MB | 22 MB anon |
| bare `QApplication` | 18 MB | |
| minimal Qt Quick window | 50 MB | |
| minimal Widgets window | 26 MB | |
| the app on the author's machine | ~90 MB | 200 MB RSS |
| the same, with `QT_QPA_PLATFORM=offscreen` | ~53 MB | 117 MB RSS |
