# Adding a FormID to `CompletePlanetSurvey.esm` without shifting existing IDs

Background: `.claude/skills/starfield-modding/references/creation-kit.md` (CK basics, why we don't
just re-save the master) and `.claude/skills/starfield-modding/references/record-types.md` (record/
group binary layout). This doc is the project-specific **procedure**, not a general ESM-format
tutorial — read those first if the terms below (`GRUP`, subrecord, `HEDR`) are unfamiliar.

## Why this exists

`Data/CompletePlanetSurvey.esm` holds every Settings → Gameplay toggle this mod exposes (Hand
Scanner `0x80C`, Orbital Scanner `0x80D`, and the `CPSRecallMessage` popup `0x807` — the full,
centralized list is the "PINNED ESM FORMIDS" block at the top of
`Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc`). Papyrus resolves every one of these by
**literal FormID** via `Game.GetFormFromFile(id, "CompletePlanetSurvey.esm")` — there is no
editor-id lookup at runtime.

**The Creation Kit reassigns FormIDs on every save of a master file**, and it can't edit a master
"in place" the way it edits an `.esp` — the moment you save, records can renumber. Do that to
`CompletePlanetSurvey.esm` and every pinned literal above silently starts resolving to the wrong
form (or `None`) for every player who updates, with no crash and no obvious symptom beyond a
toggle that mysteriously stops doing anything. `ResolveEsmForm` (see the .psc file) now logs a
named ERROR when a pinned form goes missing, but a **renumbered-not-missing** form is worse: it
resolves to *some* form, silently wrong, and logs nothing.

**Rule: never open `CompletePlanetSurvey.esm` in the CK and save it once it has shipped.** To add
a brand-new record (e.g. a new Settings toggle), binary-splice it into the existing file instead —
every existing FormID stays byte-identical.

## The procedure (binary surgery, not CK)

This is how the Orbital Scanner toggle (`0x80D`, v1.4.0) was added on top of the original CK-authored
`0x80C`. No script from that session is checked into this repo (it was a one-off run against a local
copy of the ESM) — this checklist is what to reproduce.

1. **Locate the template.** Find an existing `GPOF` (GameplayOption) record to clone — its subrecord
   layout (value type, default, min/max, category, display flags) is your working template. Read it
   in SF1Edit/xEdit first so you know what every subrecord means before you touch bytes.
2. **Clone the record.** Copy the template `GPOF` record's bytes verbatim. Assign the clone a
   **brand-new FormID** — the next unused local id in this master's own range (e.g. `0x80E` if
   `0x80D` is the highest in use). Never reuse an id that's in the centralized Papyrus block or
   anywhere else in the file.
3. **Edit the clone's display-only subrecords.** `EDID` (editor id — change it, must be unique),
   `NNAM`/`DNAM` (display name / description) are safe to change freely: **Papyrus keys on the
   FormID, never the editor id or display text**, so renaming these cannot break anything at
   runtime. Leave the value/behavior subrecords (default, min/max, category/group linkage) matching
   the template unless you deliberately want different toggle semantics.
4. **Register the clone with its `GPOG` parent.** Find the `GPOG` (GameplayOptionGroup) record that
   owns the Settings-menu section this toggle belongs to, and append the new record's FormID
   (little-endian `u32`) to its `GOGL` subrecord (the member list) — this is what makes the toggle
   actually appear in the Settings → Gameplay panel, in list order.
5. **Fix every enclosing size field.** Adding bytes anywhere in the file invalidates every size that
   counts them:
   - The new record's own containing `GRUP` (record group) — its `size` field covers all records
     inside, so it grows by the new record's total on-disk size.
   - Every **ancestor** `GRUP` above that one, up to (but not including) the top-level file, all grow
     by the same amount (nested groups are additive).
   - The `GPOG` record's own size grows by 4 bytes (one new `u32` FormID appended to `GOGL`).
   - The master's `TES4` header `HEDR` subrecord carries a **record count** field — increment it by
     exactly 1 (one new record was added; group headers themselves don't count).
   Miss any one of these and the file is malformed — tools that trust the size fields (rather than
   walking byte-by-byte) will misparse everything after the edit.
6. **Write the file, then verify in xEdit/SF1Edit — do not trust the write:**
   - Open the new `CompletePlanetSurvey.esm` and confirm the `TES4` header's record count matches
     what xEdit itself counts.
   - Confirm the new `GPOF` appears under the intended `GPOG`, with the display name/description you
     set and the default value you intended.
   - **Diff every existing FormID against the previous build** — `0x807`, `0x80C`, `0x80D` (and
     any other id already in the Papyrus PINNED ESM FORMIDS block) must resolve to the *exact same*
     record type/name as before. This is the step that actually protects existing saves and the
     pinned Papyrus literals — don't skip it because the rest looked clean.
7. **Update the Papyrus side.** Add the new FormID to the centralized block in
   `CompletePlanetSurveyQuest.psc` (see the "PINNED ESM FORMIDS" comment block near the top of that
   file) with the same one-line-per-id style, and route every lookup through `ResolveEsmForm`. Don't
   add a second ad hoc `Game.GetFormFromFile` literal elsewhere.
8. **In-game smoke test (user-verified, not automatable here):** load a save, open Settings →
   Gameplay, confirm the new toggle is present with the right default, flip it, save, reload, confirm
   the value round-tripped. Confirm the *other* toggles are still present, in the same order, with
   their saved values intact — the surest sign nothing upstream shifted.

## What NOT to do this way

This procedure is for **adding a brand-new record** only. It is not a substitute for the CK when you
need to *change* an existing record's structural fields (not just display text) — anything beyond
appending a clone-and-relink record risks a subtly malformed file that only some tools catch. For any
edit fitting that shape, prefer a child `.esp` that overrides the record instead of hand-patching the
master (the CK can edit an `.esp` freely; the "can't edit a master in place" constraint is specific to
this file being an `.esm`, which it is *for the FormID stability this checklist exists to protect*).
