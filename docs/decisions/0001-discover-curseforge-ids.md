# 0001: Discover CurseForge ids at runtime

Date: 2026-09-27
Status: accepted

## Context
Search returned nothing because the code filtered on a hardcoded
`classId=4471`, which is Minecraft Modpacks: CurseForge class ids are global
across games. `--flavor` never matched because flavor is a numeric
`gameVersionTypeId`, not text in a file's `gameVersions`.

## Decision
Look up WoW's "Addons" class from `GET /v1/categories?gameId=1` and the
flavors from `GET /v1/games/1/version-types`, cache them for the process
lifetime (flavors also in `flavors.json`), and filter on the server by id.

## Consequences
New Classic re-releases appear without a code change. Never add a numeric
CurseForge id as a constant.
