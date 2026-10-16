# GUI development

## QML files

The views are small components under `src/gui/qml/`, so a change usually
touches one file.

| File | What it is |
| --- | --- |
| `Main.qml` | window, sidebar, popups wiring; pages live in a `Kirigami.PagePool` |
| `InstalledPage.qml` | the AddOns page: toolbar, list, remove dialog, which rows and changelogs are open. Every row keeps a placeholder of its last known height and the real `AddonRow` is only loaded near the viewport, which keeps the scrollbar exact |
| `AddonRow.qml` | one installed addon: header line; raises signals, owns no state |
| `AddonDetails.qml` | the expandable details (facts, folders, changelog, buttons) |
| `ChangelogBox.qml` | markdown changelog on a darker panel, with the inline "Show more" |
| `AppColors.qml` | the colour palette singleton (page background, inset field colour, border) |
| `StyledPage.qml` | base of every page, gives them the shared background |
| `AddonMeta.qml` | the facts line under a description (downloads, date, version, flavor; empty ones are left out), shared by Search and AddOns |
| `AuthorLine.qml` | the "By Author" line under an addon's title (hidden while the author is unknown) |
| `FolderChip.qml`, `AddonIcon.qml`, `FieldNote.qml` | small shared pieces |
| `SearchPage.qml`, `SettingsPage.qml`, `InstallDialog.qml`, `UpdatesDialog.qml`, `ScanDialog.qml` | the other pages and popups |

## Conventions

- Components with knobs (colours, line counts, texts, icon names) keep them in
  a `Tunables` block at the top.
- Colours come from `AppColors`: three fixed hex colours sampled from the
  Breeze Dark settings page, so surfaces look the same on any colour scheme.
  Text and icons still follow the system theme.
- Icons are Breeze icon names.
- Open/expanded state that must survive scrolling lives on the page, not on
  the row: `ListView` destroys rows that scroll out of view.
- `AddonDetails` receives the list row as `addon`, so a new model role is
  usable there as `addon.<role>`.

## Live reload

```sh
env WAM_QML_DIR=(pwd)/src/gui/qml ./build/wam-gui    # fish
WAM_QML_DIR=$PWD/src/gui/qml ./build/wam-gui         # bash/zsh
```

The app loads QML from disk and reloads the window whenever a `.qml` file in
that folder is saved. Syntax errors are printed on the terminal and the window
comes back once fixed. Without the variable, the QML compiled into the binary
is used.

## Adding a QML file

1. Add its name to `WAM_QML_FILES` in `CMakeLists.txt` (every file sits flat in
   the module via `QT_RESOURCE_ALIAS`).
2. If it is a singleton, also mark it `QT_QML_SINGLETON_TYPE` in
   `CMakeLists.txt` and list it in `src/gui/qml/qmldir`. That `qmldir` exists
   only for live-reload mode and is the hand-kept twin of the generated one.

## Style

`main.cpp` selects the KDE Quick Controls style (`org.kde.desktop`) before Qt
starts, unless `QT_QUICK_CONTROLS_STYLE` is already set. Runtime needs
Kirigami and `qqc2-desktop-style`.
