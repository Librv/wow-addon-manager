# Agent guide

This file is the entry point for AI coding agents (Claude Code, Codex, Cursor
and others) working in this repo. `CLAUDE.md` imports it, so there is one copy
to keep current. The README is for people using the app; this file and
`docs/` are for people and agents changing it.

## What this is

A World of Warcraft addon manager for Linux, in C++20. One engine, two front
ends:

- `src/core/` builds `wam_core`: CurseForge client, installer, state, `.toc`
  reader, reconciler. No Qt dependency, ever.
- `src/cli/` builds `wam`, a CLI over everything the core can do.
- `src/gui/` builds `wam_gui` (Qt adapter: worker thread, controller, list
  models) and `wam-gui` (the Kirigami app, QML in `src/gui/qml/`).

Read `docs/architecture.md` before changing how these pieces talk to each
other.

## Build and test

```sh
cmake -S . -B build                 # add -DWAM_BUILD_GUI=OFF for core + CLI only
cmake --build build -j
ctest --test-dir build --output-on-failure
```

All tests are offline: no network, no API key. Run them before every commit.
After changing a header, use `cmake --build build --clean-first` (see
`docs/gotchas.md`). To run the GUI with live QML reload:

```sh
env WAM_QML_DIR=(pwd)/src/gui/qml ./build/wam-gui    # fish; bash: WAM_QML_DIR=$PWD/src/gui/qml
```

## Knowledge base

Read the doc that matches the task before starting it.

| Doc | Read it when |
| --- | --- |
| `docs/architecture.md` | changing anything across core, adapter, CLI or GUI; threading; state files; the install swap |
| `docs/gui.md` | touching QML or the Qt models |
| `docs/curseforge-api.md` | calling or parsing anything from CurseForge |
| `docs/testing.md` | adding tests, or claiming something is verified |
| `docs/gotchas.md` | a build, link or Qt warning looks strange |
| `docs/decisions/` | about to change a design choice; check whether it was made on purpose |
| `docs/ram-testing.md` | measuring memory use |

Area rules also live next to the code: `src/core/AGENTS.md` and
`src/gui/AGENTS.md`.

## Rules

These always apply.

1. **The core stays Qt-free.** Nothing under `src/core/` includes Qt.
2. **The GUI thread never blocks.** All network and disk work goes through
   `WamWorker` on its own thread. `WamController` only forwards with queued
   calls. See `docs/architecture.md`.
3. **Never hardcode CurseForge ids.** Class ids and flavor ids are discovered
   from the API at runtime. A guessed constant already broke search once
   (`docs/decisions/0001-discover-curseforge-ids.md`).
4. **Every install goes through `AddonInstaller::installZip`.** It stages,
   backs up, swaps and rolls back. Do not write into `AddOns/` any other way,
   and keep the path guards (single plain path component, no `..`, no
   absolute zip entries).
5. **Old state files must keep loading.** New fields in `installed.json`,
   `config.json` or `flavors.json` get defaults, and a test loads an older file.
6. **Test what matters, and only that.** A change that could break something
   a user relies on gets a test that fails when it breaks, in the existing
   hand-rolled `check()` style. No test framework dependency. Follow the
   rules in `docs/testing.md`.
7. **New QML files** go in `WAM_QML_FILES` in `CMakeLists.txt`. A new singleton
   also goes in `src/gui/qml/qmldir`.
8. **Say what was and was not verified.** Nothing here has a live CurseForge
   check in the tests. If you could not run something (the GUI, the live
   API), say so; do not claim it works.
9. **Keep the docs true.** A change to behaviour, a rule or a layout updates
   the matching doc (README for users, `docs/` or this file for developers) in
   the same commit. A new design choice that someone might later undo gets a
   short record in `docs/decisions/`.
10. **No long sleeps.** Do not `sleep` while building, testing or running the
    app unless nothing else works. Wait on a condition instead (poll with a
    short bound, like `waitFor` in the GUI tests). If a sleep is unavoidable,
    keep it to a few seconds.

## Writing style

Applies to code comments, docs, commit messages, PR text and UI strings.

- **Never use em-dashes** (the long dash, U+2014). Use a colon, comma,
  parentheses or a new sentence instead. Before committing, check with
  `grep -rnP '\x{2014}' --exclude-dir=build --exclude-dir=.git .`; it should
  print nothing.
- Plain, short sentences. Say what something does and why, not how clever it is.
- UI text uses sentence case ("Check for updates") and the single ellipsis
  character `…` for items that open a dialog or show progress
  ("Choose zip…", "Loading…").

## Code style

- C++20, `#pragma once`, namespaces `wam` (core) and `wam::gui` (adapter).
- Comments explain why or give context the code cannot. Short trailing
  comments on struct fields say what an empty or zero value means.
- QML components keep their knobs (colours, sizes, line counts, texts) in a
  `Tunables` block at the top. Colours come from `AppColors`.
- Match the surrounding code before reaching for a new pattern.

## Commits

- Summary line in the imperative, sentence case, no prefix, no trailing period,
  describing the user-visible result ("Keep open changelogs open when their row
  scrolls away").
- A body paragraph saying what was wrong or missing and what changes.
- Commit straight to `main` unless asked otherwise; there is no CI yet, so
  run the tests locally first.
