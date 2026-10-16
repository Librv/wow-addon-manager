# 0003: Identify existing addons by `.toc` project id

Date: 2026-09-27
Status: accepted

## Context
Users already have addons installed by hand or by other managers. wam needs
to know which CurseForge mod each folder belongs to.

## Decision
Read `## X-Curse-Project-ID:` from each folder's `.toc` (the same heuristic
WowUp uses). Adopting records the folders with `fileId = 0` and changes
nothing on disk. With an API key, sibling modules are claimed through the
mod's files' `moduleNames`.

## Consequences
The mod is known, the version is not, until the first update pins a real
file. Untagged folders need a manual mod id.
