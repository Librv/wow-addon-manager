# 0005: Hand-rolled test harness

Date: 2026-09-27
Status: accepted

## Context
The project prefers light tooling and few dependencies.

## Decision
Tests are plain executables using a small `check(condition, description)`
helper, registered with CTest. No test framework.

## Consequences
Keep new tests in that style. Each suite prints `ok:` / `FAIL:` per check and
exits non-zero on any failure.
