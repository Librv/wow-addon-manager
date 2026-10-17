# 0008: Profiles share a library of zips and switch by diff

Date: 2026-10-17
Status: accepted

## Context
Users want named sets of addons (profiles, like Minecraft modpacks) and to
swap between them without downloading everything again. WoW only loads one
`AddOns/` folder per install, so only one set can be live per folder.

## Decision
- Each profile has its own WoW folder, flavor and `installed.json` with
  pinned file ids. At most one profile is active per WoW folder.
- Every zip a profile uses is kept in `library/<modId>/<fileId>.zip`, shared
  by all profiles. Addons without a zip (adopted ones) are snapshotted from
  their folders before they leave `AddOns/`.
- Inactive profiles are edited in the library only; `AddOns/` is not touched.
- Activating a profile diffs it against the current active one: shared
  addons at the same file id stay, the rest are removed or installed with
  `installZip` from the library. A plan file (`profiles/switch.json`) lets an
  interrupted switch finish on the next start.

## Consequences
- `installZip` stays the only way into `AddOns/` (0002); profiles reach it
  through `ProfileOps`.
- Zips are kept until no profile refers to them, which costs disk space but
  keeps blocked downloads recoverable.
- Symlinking `AddOns/` per profile was rejected: it bypasses `installZip`,
  stores shared addons once per profile, and is untested under Wine.
- Settings in `WTF/` (SavedVariables) are not swapped. Each addon keeps its
  settings across profiles.
