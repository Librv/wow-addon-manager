# 0002: Transactional installs through one function

Date: 2026-09-29
Status: accepted

## Context
A failed or interrupted install must not leave a half-replaced addon in the
user's game folder.

## Decision
`AddonInstaller::installZip` is the only path into `AddOns/`. It extracts into
`.wam-staging`, moves old folders into `.wam-backup`, renames the new ones in,
and deletes the backup; any failure rolls back. A leftover `.wam-backup` is
restored on the next run.

## Consequences
New install sources (manual zips, future GitHub or Wago) must call it too.
There is no lock between processes, so only one `wam` or `wam-gui` should run
at a time.
