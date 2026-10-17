# Decisions

Short records of design choices that someone might otherwise undo. One file
per decision, numbered: `NNNN-short-name.md`.

Template:

```markdown
# NNNN: Title

Date: YYYY-MM-DD
Status: accepted | replaced by NNNN

## Context
What problem or constraint led to this.

## Decision
What we do.

## Consequences
What this costs, and what to keep in mind when changing nearby code.
```

| # | Decision |
| --- | --- |
| 0001 | [Discover CurseForge ids at runtime](0001-discover-curseforge-ids.md) |
| 0002 | [Transactional installs through one function](0002-transactional-installs.md) |
| 0003 | [Identify existing addons by `.toc` project id](0003-adopt-by-toc-project-id.md) |
| 0004 | [One worker thread owns the core in the GUI](0004-single-worker-thread.md) |
| 0005 | [Hand-rolled test harness](0005-hand-rolled-tests.md) |
| 0006 | [Tests answer HTTP themselves](0006-fake-http-in-tests.md) |
| 0007 | [Keep the API key private](0007-keep-the-api-key-private.md) |
| 0008 | [Profiles share a library of zips and switch by diff](0008-profiles-share-a-zip-library.md) |
