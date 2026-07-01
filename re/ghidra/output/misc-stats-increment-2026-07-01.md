# Misc-stat (character Statistics panel) increment mechanism — 2026-07-01

**Goal:** our ref-free completion updates the survey knowledge DB but NOT the player
"Statistics" panel counters (Flora Fully Scanned / Fauna Fully Scanned / Unique
Creatures Scanned / Planets Scanned). Natural scans increment those; we bypass them.

## The tracked-stats table (global)

Classic Bethesda tracked-statistics table. Confirmed from the scan→stat handlers
`ID_100392` (Planets Scanned) and `ID_100393` (Flora/Fauna/Unique Creatures), and the
Stats-menu display fn `ID_88202`:

- `ID_889375` — global, **entry count** (uint32).
- `ID_889377` — global, **table base pointer** (heap array; stride **0x20**).
  - entry `+0x00` : interned stat-name pointer (BSFixedString value).
  - entry `+0x10` : **int32 counter** — the authoritative, displayed & saved value.
- `ID_894532` — global char, "stats enabled" gate (handlers early-out if 0).

**Increment = linear scan** the table for the entry whose `+0x00` == the target
name pointer, then `*(int*)(entry+0x10) += 1`. The Stats MENU (`ID_88202`) reads the
value straight from `entry+0x10` (`param_1 = *(int*)(i*0x20 + 0x10 + ID_889377)`), so
that field is what shows on the panel — incrementing it is sufficient for display.

After the counter bump the handlers also do `*(int*)(nameGlobal+0x10)++` under LOCK and
fire a change event via `ID_64175()` (event source) + `ID_123824(...)`. That path is a
**live-UI notification** (updates an already-open Stats menu); the persistent value is
`entry+0x10`. We can skip the event — the value is re-read when the panel next opens.

## Stat-name string globals (interned; used as the table-scan key)

Each is a `BSFixedString` global holding the interned pointer the handlers compare against:

| Stat display name          | name-global | table match in |
|----------------------------|-------------|----------------|
| "Planets Scanned"          | `ID_923216` | ID_100392      |
| "Flora Fully Scanned"      | `ID_923219` | ID_100393 (cVar5=='.') |
| "Fauna Fully Scanned"      | `ID_923220` | ID_100393 (cVar5=='2') |
| "Unique Creatures Scanned" | `ID_923223` | ID_100393 (obj+0x11 flag) |

(`sMiscStat*` GMSTs — sMiscStatFloraFullyScanned etc. — are just the settings that map
the internal key to the display string; not needed for the write.)

## Handler gating (what a natural scan keys on), from ID_100393

`param_2` = scanned-object event. `cVar5` = an object class char resolved via
`ID_47401(obj+0xc)` / `ID_37104(&ID_888147,...)`:
- `obj+0x10 (byte) >= 100` (fully scanned %) AND `cVar5=='.'` → Flora Fully Scanned++.
- `... >= 100` AND `cVar5=='2'` → Fauna Fully Scanned++.
- `obj+0x11 != 0` (unique flag) → Unique Creatures Scanned++ (independent of flora/fauna).
So **Unique Creatures is a SUBSET flag on the object**, not "== fauna count". Matching it
exactly needs the per-species unique flag from the ESM; approx = fauna-species count.

## Implementation plan (native, crash-guarded, no heap-array poke)

A native `IncrementMiscStat(nameGlobalId, delta)`:
1. `base = *(uintptr_t*)REL::ID(889377)`, `count = *(uint32_t*)REL::ID(889375)`,
   `enabled = *(char*)REL::ID(894532)`. Null/zero-guard all; bail if !enabled or !base.
2. `namePtr = *(uintptr_t*)REL::ID(<923216|923219|923220|923223>)`.
3. Scan `i in [0,count)`, `entry = base + i*0x20`; if `*(uintptr_t*)entry == namePtr`
   then `*(int32_t*)(entry+0x10) += delta; return`. Bounded loop, in-place int add — NOT
   a BSTArray grow, so the heap-corruption rule doesn't apply.

REL::IDs to bind: 889375, 889377, 894532, 923216, 923219, 923220, 923223.
Deltas from our green path: flora-species-count → Flora, fauna-species-count → Fauna,
(unique subset TBD) → Unique Creatures; optionally +1 Planets Scanned per planet.

Decompiles archived: scratchpad increment.c / stathandlers.c / tablexrefs.txt (session).
