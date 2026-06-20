# Nexus mod page content

Source of truth for the [Nexus mod page](https://www.nexusmods.com/starfield/mods/16493)
description text. Edit here, then copy into the Nexus editor.

| File | What it is | Where it goes on Nexus |
|---|---|---|
| `full-description.bbcode` | Full description, in Nexus BBCode | Mod page → Description |
| `short-description.txt` | One-paragraph summary | Mod page → Summary |

## Pasting into Nexus

Nexus's description editor is WYSIWYG, but it accepts BBCode: switch the editor to
its **BBCode / source view** (the `[BB]` / source toggle) and paste
`full-description.bbcode` there, then switch back to preview to confirm it rendered.
Pasting BBCode into the rich (WYSIWYG) view directly shows the tags literally.

The summary field is plain text — paste `short-description.txt` as-is.

## Keep in sync

When a release changes player-facing behaviour, update these two files in the same
PR as `CHANGELOG.md`, then re-paste into Nexus when publishing.
