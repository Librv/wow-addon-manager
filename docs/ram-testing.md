# RAM testing

`tools/ram-test.sh` launches the real `wam-gui` (the repo's own QML, real Kirigami) idle
and reports RSS, PSS and anonymous memory. It is meant for A/B comparison of code
changes, not for absolute numbers.

```sh
tools/ram-test.sh                 # empty state, 3 runs
tools/ram-test.sh --seed 100      # 100 fake tracked addons
tools/ram-test.sh --software      # QT_QUICK_BACKEND=software
tools/ram-test.sh --setup-only    # just install deps and build Kirigami
```

Work files go to `~/.cache/wam-ramtest` (override with `WAM_RAMTEST_DIR`). The first run
builds Kirigami, which is slow on one core; later runs only rebuild wam-gui.

## Why it looks the way it does

Written for the Ubuntu 24.04 sandbox: 1 core, about 3 GB RAM, Qt 6.4.2, no Kirigami 6
package, no `qqc2-desktop-style`, no GPU or Wayland, and no access to
`api.curseforge.com`. So the script builds Kirigami 6.0.0 from the KDE GitHub mirrors with
small Qt 6.4 patches, compiles the repo's sources in a scratch project, and runs on Qt's
`offscreen` platform. Only `main.cpp` differs from the repo (Qt 6.4 lacks
`loadFromModule`); the QML is loaded unmodified through `WAM_QML_DIR`. Kirigami script
warnings on stderr are expected on Qt 6.4.

## Reading the results

- Compare PSS and anonymous memory. RSS counts shared libraries in full.
- Compare only runs from the same setup. The Wayland and GPU path adds roughly 40 MB PSS on
  a real desktop and is absent here.
- Reference points (idle, empty state): this script ~70 MB PSS / 22 MB anon; bare
  `QApplication` 18 MB PSS; minimal Qt Quick window 50 MB; minimal Widgets window 26 MB.
  On the author's machine the GUI is ~90 MB PSS / 200 MB RSS normally and ~53 MB PSS /
  117 MB RSS with `QT_QPA_PLATFORM=offscreen`.
- With no API key the app makes no CurseForge requests, so search results, icons and
  update checks are not exercised. `--seed` only fills the installed list.
