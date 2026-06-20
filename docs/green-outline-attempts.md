# Green flora/fauna outline — attempts matrix

**Goal:** flora/fauna render GREEN (scanner outline), persistently (survives
save/reload + greens fresh/never-individually-scanned instances), for ALL planets
from ONE atomic command. No per-planet visiting, no auto-on-arrival.

**Ground truth (user-confirmed):** a planet completed to 100% in a save, after a full
game restart + character load + flying there, has its flora/fauna spawn GREEN — fresh
instances, never individually scanned. So the green is a **persistent, per-TYPE** record
in the saved knowledge DB; objects read it on load. It does NOT require objects loaded
when it's written.

## Confirmed facts

| Fact | Status |
|---|---|
| Survey **DATA** (%, slate, traits, resources) — writable ref-free, ALL planets, one command | ✅ WORKS (`CompleteAllPlanetsSurveyData`: 1798/1798) |
| **Green** = the engine's full scan completing a type (the in-world scan event), 1 scan with our instant-scan setting | ✅ confirmed |
| Full scan credits the planet **`ID_52188` resolves from the ref's location** = the planet you're standing on | ✅ confirmed — the core blocker for "all planets" |
| The green outline reads a per-TYPE red-black tree at `subobj+0x60`, saved | ✅ confirmed |
| The `db+0x3d8` catalog key's **3rd component is per-instance** (varies per spawn, even after scanning) | ✅ confirmed (2-spawn stability test) |

## Approaches tried

| # | Approach | What it writes | Result | Why |
|---|---|---|---|---|
| 1 | `IncrementScanFlag` `+0x21`/`+0x20` (ID_124898) | survey count byte | 🟦 BLUE (data 100%) | outline never reads `+0x21`; setting the byte ≠ firing the completion |
| 2 | `ID_83038` write `(939118, species)+0x28` | per-instance scanned byte | 🟦 BLUE | `ID_126806` find-only → no entry for unscanned species → no-op; also keyed per-instance |
| 3 | `ID_124902` tree write with `vtable[0x15]` key | green tree | 🟦 BLUE | `vtable[0x15]` returns `{0,0,0}` for FLOR/NPC_ base forms → no key |
| 4 | Capture catalog key (`ID_126718`) off a spawn + stamp trees (`ID_124902`) | green tree, all planets | 🟦 BLUE | catalog key 3rd component is per-instance → fresh instances never match |
| 5 | **Spawn + SetScanned + UpdatePlanetProgress (+ ScanNearbyRefs)** — `CompleteSurvey` | the engine's full scan | 🟩 **GREEN** (current planet, fresh instances) | this is the real engine completion — but credits the CURRENT planet only |
| 6 | `ID_52161` direct, explicit target planet | green tree | 🟦 BLUE | tree write alone isn't the green; key from spawned instance is per-instance |
| 7 | `ID_52161` direct + `UpdatePlanetProgress` (count completion), current planet | tree + count | 🟩 **GREEN** | tree write + count completion together fire the "type surveyed" seed. THIS IS THE ANSWER (current planet) |
| 8 | `ID_52161`(target) + `ID_52158`(target) direct — both with explicit target planet, looped over all planets | tree + count, all planets | ✅ **GREEN — CONFIRMED on a never-visited planet** | option A. `CompleteTypeForPlanet` = `ID_52158`-direct (context confirmed from `ID_52157`). `GreenAllPlanets` = spawn 1 handle/species → `GreenSpeciesEverywhere` (tree+count per host planet). Ran on Jemison; flew to a NEVER-VISITED planet → flora/fauna GREEN. **THE SOLUTION.** |

## RESOLVED — the answer

**Recipe (per planet, ref-free for any target):** `ID_52161` (tree write, planet explicit) +
`ID_52158` (count completion, planet explicit). The tree write alone is blue (#6); the pair greens
(#7 current planet, #8 never-visited planet). The biome-propagation risk did **not** materialize —
`ID_52158` reading the current biome for its cluster propagation was not load-bearing for the green;
the planet-explicit scan-flag/percent + tree are sufficient.

**Atomic delivery:** `CompleteAllPlanetsSurveyData` (one console command) = data sweep (1798/1798)
+ trait pass (1121) + `GreenAllPlanets` (1097 unique species spawned one at a time as handles,
1711 planet-types greened). Confirmed: completed on Jemison, a never-visited planet shows green.

## What this tells us

- The ONLY thing that has produced green is **#5 (the engine's real scan)**, and it
  greens the planet you're physically on.
- Everything that tried to *write the persistent state directly* (1,2,3,4,6) failed —
  the green is produced by the engine's **completion logic firing**, not by any single
  byte/tree we can set.
- So the atomic-all-planets question reduces to: **can the engine's completion be fired
  for a TARGET planet (not the one you're on)?** `ID_52188` ties it to the ref's location.

## Remaining options (not yet exhausted)

| Opt | Idea | Risk / unknown |
|---|---|---|
| A | `ID_52161`(target) + `ID_52158`(target) directly — both completion writers, planet as explicit arg | does the count-completion green seed need the ref/biome? (TESTING #7 first, current planet) |
| B | Override the spawned ref's location (`ExtraData 0x81` on the base form, read first by `ID_52188`) to the target planet, then run the proven scan (#5) | can we attach/set ExtraData 0x81 on a ref via CommonLibSF? unproven |
| C | Decompile `ID_83019` (biome-completion event) fully + find what persistent per-type "surveyed" flag it/the completion sets, write that flag ref-free per planet | more RE; the flag may itself be the elusive green seed |
| D | (rejected by user) per-planet on visit / auto-on-arrival | — |

## Next concrete step

Validate #7 on the current planet (tree + count). If GREEN, build option A (target-planet
count writer `ID_52158`-direct to pair with the target-planet tree writer). If BLUE, the
seed is elsewhere → option C (find the persistent "type surveyed" flag the completion sets).
