# 0009: Paint list row highlights only on hover

Date: 2026-10-17
Status: accepted

## Context
With the `org.kde.desktop` style, an `ItemDelegate` background is two
`StyleItem`s that each paint a full-row image, for every row, hovered or not.
That cost about 1 MB per loaded row. Building `AddonDetails` and the row menu
up front added about 0.8 MB more per row.

## Decision
- List rows use `ListRowBackground`, a `Loader` that is active only while the
  row is hovered, pressed, focused or highlighted.
- It loads `NativeRowBackground`, which wraps the style's private
  `DefaultListItemBackground`, by file name. The private import is therefore
  resolved only on demand.
- If that load fails, a Rectangle in the theme highlight colour at low
  opacity shows instead.
- `AddonDetails` and the row menu are also created on demand.

## Consequences
- Idle rows hold no style images, so memory per loaded row drops by about
  2 MB.
- `org.kde.desktop.private` is private API and may change between KDE
  releases. When it does, the fallback tint shows: the highlight looks
  different, the row still works.
- New list rows should use `ListRowBackground` instead of the default
  `ItemDelegate` background.
