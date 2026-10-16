# Testing

## Suites

| Binary | Source | Covers |
| --- | --- | --- |
| `build/tests/wam_tests` | `tests/test_main.cpp` | zip extraction and its guards, the install swap, rollback, crash recovery, state round-trips (including old and damaged state files), config (including its file mode), `.toc` reading, reconciliation and adoption, flavor matching and cache, HTML-to-text, parsing of sample CurseForge JSON, HTTPS-only requests |
| `build/tests/wam_flavor_tests` | `tests/flavor_names_test.cpp` | flavor display names |
| `build/tests/wam_gui_tests` | `tests/gui_tests.cpp` | the adapter layer: worker thread, controller, models (install window version list, installed row texts, changelog state, flavor list, scan and adopt through the real worker), manual installs, and update check, apply, failed download and blocked download against a fake CurseForge |

```sh
ctest --test-dir build --output-on-failure
```

Everything runs offline, with no real API key. On a machine with no display,
run the GUI tests with `QT_QPA_PLATFORM=offscreen`. Each suite has a 30 second
CTest timeout; they finish in well under a second.

Each test binary first points `XDG_CONFIG_HOME` and `XDG_DATA_HOME` at a temp
folder (so no test can touch your real config and key) and sets
`HttpClient::transportForTesting` to refuse every request. A test that needs
CurseForge scripts `FakeCurseForge` in `tests/gui_tests.cpp` with the exact
requests it expects and their responses; anything unscripted is reported as
a failure. See `docs/decisions/0006-fake-http-in-tests.md`.

## Rules for tests

- **A test must fail when the code breaks in a way that matters.** "Matters"
  means a user would notice: an addon is lost or half-installed, a file lands
  outside `AddOns/`, an older state file stops loading, the wrong version is
  offered, the GUI shows wrong data or hangs. Prove it: break the code on
  purpose (or revert the fix) and watch the test fail before you keep it.
- **Write the 20% that catch 80% of the bugs.** The install swap, rollback
  and recovery, path guards, state and config loading, update selection,
  CurseForge parsing fallbacks, and the worker round trip come first. Skip
  tests of plain getters, model roles that only return a field, string
  constants, anything the compiler already checks, and `check(true, ...)`.
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

2026-10-16, CachyOS, Qt 6.12.0, Kirigami 6.31.0: all three suites pass
(`wam_tests` 141 checks, `wam_flavor_tests` 10, `wam_gui_tests` 150).

Earlier history: the first versions were written in a sandbox (Ubuntu 24.04,
Qt 6.4.2, no Kirigami, no access to `api.curseforge.com`), where only the
core, the adapter tests and a QML syntax lint could be checked. That is why
some API details in `docs/curseforge-api.md` are still marked as docs-only.

Update this section when the numbers or the environment change.

## Memory

See `docs/ram-testing.md` for `tools/ram-test.sh`.
