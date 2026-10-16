# 0004: One worker thread owns the core in the GUI

Date: 2026-09-29
Status: accepted

## Context
The core blocks on network and disk and is not thread-safe. The window must
stay responsive.

## Decision
`WamWorker` runs on one dedicated thread and owns `Config`, `StateStore` and
`CurseForgeClient`. `WamController` forwards to it with queued calls only.

## Consequences
The core needs no locking. Requests run strictly in order, so a click waits
behind work already queued (for example Apply behind a running update check).
