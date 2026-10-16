# Gotchas

Things that have cost time before. Add to this list when something surprising
takes more than a few minutes to figure out.

## Build

- **Stale objects after a header change.** Symptoms: "undefined reference" to
  something plainly defined (like `CurseForgeClient::getMods`), or odd
  behaviour after a struct changed. Cause: files with timestamps older than
  the build outputs, often from copying or extracting. Fix:
  `cmake --build build --clean-first`, after any header change.
- **`find_package(libzip)` breaks on Debian/Ubuntu**, whose CMake config
  references helper binaries that may be missing. The build uses pkg-config
  for libzip instead; keep it that way.
- **Qt 6.5 is the floor** because `main.cpp` uses
  `QQmlApplicationEngine::loadFromModule`.
- **New QML file not found** in a normal build: it is missing from
  `WAM_QML_FILES`. Singleton not found in live-reload mode: it is missing from
  `src/gui/qml/qmldir`.

## Runtime

- **"Could not register app ID"** from the portal on startup: the `.desktop`
  file is not installed. `cmake --install build --prefix ~/.local`.
- **"Created graphical object was not placed in the graphics scene"** on tab
  switch: caused by Kirigami's `PageRow` creating pages under a plain
  `QtObject`. Pages live in a `Kirigami.PagePool` to avoid it; do not go back
  to pushing Components or URLs.
- **`[undefined]` to `QString` binding errors**: a `QVariantMap` handed to QML
  is missing a key. Fill every key on the C++ side.
- **Expanded rows closing or the list jumping while scrolling**: `ListView`
  destroys off-screen rows, and guesses heights of rows it has not loaded.
  Keep open state on the page and use the placeholder rows in
  `InstalledPage.qml`.

## CurseForge

See "Facts that are easy to get wrong" in `docs/curseforge-api.md`: global
class ids, and flavor being a numeric `gameVersionTypeId`.
