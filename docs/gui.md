# GUI development

## QML files

The views are small components under `src/gui/qml/`, so a change usually
touches one file.

| File | What it is |
| --- | --- |
| `Main.qml` | window, sidebar, popups wiring; pages live in a `Kirigami.PagePool` |
| `ProfileTabs.qml` | the profile tabs above the page: select, **New profile…**, the **Rename…** / **Delete…** menu |
| `ProfileNameDialog.qml` | the name dialog behind **New profile…** and **Rename…** |
| `InstalledPage.qml` | the AddOns page: toolbar, list, remove dialog, which rows and changelogs are open |
| `AddonRow.qml` | one installed addon: header line; raises signals, owns no state |
| `ListRowBackground.qml`, `NativeRowBackground.qml` | hover-only row highlight for list rows (see Conventions); the second holds the private style import |
| `AddonDetails.qml` | the expandable details (facts, folders, changelog, buttons) |
| `ChangelogBox.qml` | markdown changelog on a darker panel, with the inline "Show more" |
| `AppColors.qml` | the colour palette singleton (page background, inset field colour, border) |
| `StyledPage.qml` | base of every page, gives them the shared background |
| `AddonMeta.qml` | the facts line under a description (downloads, date, version, flavor; empty ones are left out), shared by Search and AddOns |
| `AuthorLine.qml` | the "By Author" line under an addon's title (hidden while the author is unknown) |
| `FolderChip.qml`, `AddonIcon.qml`, `FieldNote.qml` | small shared pieces |
| `SearchPage.qml`, `SettingsPage.qml`, `InstallDialog.qml`, `UpdatesDialog.qml`, `ScanDialog.qml` | the other pages and popups |

## Window layout

- The sidebar is a non-modal `GlobalDrawer`; the window's content item
  already leaves room for it.
- `ProfileTabs` is a child of that content item, anchored to its top, so it
  spans the page area only. `Main.qml` pushes the page stack down with
  `pageStack.anchors.topMargin: profileTabs.height`.
- A tab click calls `wam.selectProfile(id)`. The controller reloads the
  models, so pages need no profile logic of their own beyond:
  - `InstalledPage`: the not-active note (page `header`), **Set active
    profile**, and **Scan** disabled while the profile is inactive.
  - `SettingsPage`: the folder and flavor section is labelled with the
    profile name, and the fields reset on `profilesChanged`.

## Profiles in the controller

`WamController` exposes the profiles to QML. Every member below acts on, or
describes, the selected profile (its tab).

| Member | What it is |
| --- | --- |
| `profiles` | one map per profile: `id`, `name`, `active`, `wowPath` (`""` if unset), `flavorName` (`""` if unknown), `addonCount` |
| `currentProfileId`, `currentProfileActive` | the selected profile; both notify with `profilesChanged` |
| `hasWowPath`, `wowPath`, `wowFlavorId`, `wowFlavorName` | the selected profile's folder and flavor; `setWowPath` and `setWowFlavor` change them |
| `selectProfile`, `createProfile`, `renameProfile`, `deleteProfile` | a new profile is selected at once; errors come through `errorOccurred` |
| `activateProfile`, `activatingProfile`, `profileActivated` | the switch of `AddOns/`; `activatingProfile` is true from the click until it is done or failed |

How a profile change reaches the pages:

- The worker sends `configChanged` before `profilesChanged`, so a
  `profilesChanged` handler already reads the new `wowPath`.
- On a new selected profile the controller empties the update queue and the
  scan results. Results from before the switch arrive before
  `profilesChanged` (one worker thread, in order), so none leak into the new
  profile. `applyUpdate` and `adopt` also carry the profile id and are
  refused if it is no longer selected.
- **Scan** and adopt are refused on an inactive profile: its game folder holds
  another profile's addons.

## Conventions

- Components with knobs (colours, line counts, texts, icon names) keep them in
  a `Tunables` block at the top.
- Colours come from `AppColors`: page background, inset field and border,
  all read from the system colour scheme (the Window and View colour sets).
  Do not hardcode hex colours: text follows the scheme, so a fixed dark
  surface made text unreadable on light schemes.
- Icons are Breeze icon names.
- Open/expanded state that must survive scrolling lives on the page, not on
  the row: `ListView` destroys rows that scroll out of view.
- `InstalledPage` keeps a placeholder of each row's last known height and
  loads the real `AddonRow` only near the viewport. That keeps the scrollbar
  exact while rows open and close.
- List rows (`AddonRow`, Search results, the scan list) set
  `background: ListRowBackground { control: <the delegate> }`. The desktop
  style paints a full-row image per row, about 1 MB each, so the native
  highlight is created only while a row is hovered, pressed or focused. If it
  cannot load, a plain tint shows. See `docs/decisions/0009-paint-row-highlights-on-hover.md`.
- Heavy per-row parts are created on demand: `AddonDetails` while the row is
  open, the row menu on first click.
- Dialogs are created on first use: each sits in a `Loader { active: false }`
  and is opened through a `dialogOf(loader)` helper (`Main.qml`, `ProfileTabs.qml`,
  `SettingsPage.qml`); see `docs/ram-testing.md` for what that saves. A dialog must
  read the model state when it opens, not rely on listening before then.
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
