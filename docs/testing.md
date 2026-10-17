# Testing

## Suites

| Binary | Source | Covers |
| --- | --- | --- |
| `wam_tests` | `tests/test_main.cpp` | the core (list below) |
| `wam_flavor_tests` | `tests/flavor_names_test.cpp` | flavor display names |
| `wam_gui_tests` | `tests/gui_tests.cpp` | the Qt adapter layer (list below) |

The binaries are in `build/tests/`. Run them all with:

```sh
ctest --test-dir build --output-on-failure
```

`wam_tests` covers:

- zip extraction and its guards, the install swap, rollback, crash recovery
- state and config: round trips, old files, damaged files, the config's file
  mode
- `.toc` reading, scanning and adoption
- flavor matching and the flavor cache
- HTML-to-text, parsing of sample CurseForge JSON, HTTPS-only requests
- profiles: first-run migration, damaged `profiles.json`, switching by diff,
  snapshots of adopted addons, a failed fetch, corrupt zips and failed or interrupted
  switches, damaged lists of other profiles, edits to
  an inactive profile, removing profiles and library garbage

`wam_gui_tests` covers:

- the worker thread and controller, including scan and adopt end to end
- the models: install window versions, installed row texts, changelog state,
  the flavor list
- manual installs
- against a fake CurseForge: update check, apply, failed download, blocked
  download, and where the API key is sent
- profiles: migration on first start, every key of `profiles`, creating and
  selecting, scan refused when inactive, installing into an inactive profile,
  switching `AddOns/` both ways, refetching a lost zip, and an update check
  that must not land in the next profile

## How tests stay offline and safe

- Nothing needs a real API key or the network.
- Each test binary first points `XDG_CONFIG_HOME` and `XDG_DATA_HOME` at a
  temp folder, so no test can touch your real config and key.
- Each one also sets `HttpClient::transportForTesting` to refuse every request.
- A test that needs CurseForge scripts `FakeCurseForge` in
  `tests/gui_tests.cpp` with the exact requests it expects. Anything
  unscripted fails the test. See `docs/decisions/0006-fake-http-in-tests.md`.
- Each suite has a 30 second CTest timeout; they finish in well under a
  second.
- On a machine with no display, run the GUI tests with
  `QT_QPA_PLATFORM=offscreen`.

## CI

`.github/workflows/tests.yml` runs on every push to `main` and every pull
request, in two jobs:

- **Core, CLI and GUI (Arch):** the full build and all three suites, in an
  `archlinux` container, because Ubuntu runners ship a Qt older than 6.5.
- **Core and CLI without Qt (Ubuntu 24.04):** `-DWAM_BUILD_GUI=OFF` with no
  Qt installed, so it fails if anything under `src/core/` starts needing Qt.

Neither job uses a CurseForge key or the network beyond installing packages.

## Rules for tests

- **A test must fail when the code breaks in a way that matters**, meaning a
  user would notice. For example: an addon is lost or half-installed, a file
  lands outside `AddOns/`, an older state file stops loading, the wrong
  version is offered, or the GUI shows wrong data or hangs.
- **Prove it.** Break the code on purpose (or revert the fix) and watch the
  test fail before you keep it.
- **Write the 20% that catch 80% of the bugs.** These come first: the install
  swap, rollback and recovery, path guards, state and config loading, update
  selection, CurseForge parsing fallbacks, and the worker round trip.
- **Skip tests that cannot catch a real break:** plain getters, model roles
  that only return a field, string constants, anything the compiler checks,
  and `check(true, ...)`.
- **One check per outcome.** Check the whole result a user would see, not
  every field of it on its own line.
- **No sleeps.** Wait on a condition with a short bound (`waitFor` in
  `tests/gui_tests.cpp`). A hang should fail in seconds, not minutes.

## Writing tests

- Use the hand-rolled harness: `check(condition, "what is being checked")`.
  No test framework.
- Feed CurseForge parsing with sample JSON, and flows with `FakeCurseForge`.
  Never call the live API from a test.

## What tests do not cover

- **QML.** Nothing renders the views in tests. A QML change is verified only
  by running `wam-gui`.
- **The live CurseForge API, and curl itself.** The fake checks the requests
  the app builds, not redirects, TLS or timeouts. See
  `docs/curseforge-api.md` for which endpoints are confirmed.

## Current status

2026-10-17, CachyOS, Qt 6.12.0, Kirigami 6.31.0: all three suites pass
(`wam_tests` 176 checks, `wam_flavor_tests` 10, `wam_gui_tests` 143).

Earlier history: the first versions were written in a sandbox (Ubuntu 24.04,
Qt 6.4.2, no Kirigami, no access to `api.curseforge.com`), where only the
core, the adapter tests and a QML syntax lint could be checked. That is why
some API details in `docs/curseforge-api.md` are still marked as docs-only.

Update this section when the numbers or the environment change.

## Memory

See `docs/ram-testing.md` for `tools/ram-test.sh`.
