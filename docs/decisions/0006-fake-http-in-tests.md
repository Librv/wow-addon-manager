# 0006: Tests answer HTTP themselves

Date: 2026-10-16
Status: accepted

## Context
The update check, apply and install paths all talk to CurseForge, so no test
reached them, and they are what users rely on most. Tests must never call
the real API: no key in the repo, no flaky network, no rate limits.

## Decision
`HttpClient` has one seam, `HttpClient::transportForTesting`. When set, every
`get`, `post` and `downloadToFile` goes to it instead of curl. Each test
binary sets it at startup to a transport that refuses everything, so a stray
request fails instead of reaching the network. Tests that need CurseForge
script it with `FakeCurseForge` in `tests/gui_tests.cpp`: responses keyed by
method and exact URL, every request recorded, anything unscripted reported.

## Consequences
The fake checks the URLs the client builds (paths, flavor filter) and the
headers it sends, but not curl itself: redirects, TLS and timeouts are only
exercised against the live API. Keep the seam a single static hook, like
`AddonInstaller::failAfterInstalledForTesting`, rather than threading a
transport object through the client.
