# Complete Planet Survey

**Nexus:** <https://www.nexusmods.com/starfield/mods/16493>

A Starfield mod that completes a planet's survey instantly when you scan any
single fauna, flora, or resource. Toggleable from **Settings → Gameplay**.

Built as an SFSE plugin (DLL) + a tiny ESM (toggle record) + a Papyrus glue
script. Targets Starfield 1.16.236.0 with SFSE 0.2.19.

---

## What it does

- Scan one creature, plant, or resource on a planet's surface
- The hooked engine call detours through Papyrus
- If the toggle is on, the entire planetary survey snaps to 100%: every
  flora/fauna species marked, every trait revealed, every resource registered,
  the "Survey Data" slate added to inventory
- Manual `cgf "CompletePlanetSurveyQuest.CompleteSurvey"` console command
  always works, regardless of toggle state

---

## Install

1. Install [SFSE](https://www.nexusmods.com/starfield/mods/7589) and
   [Address Library](https://www.nexusmods.com/starfield/mods/3256)
2. Install the FOMOD via Vortex / Mod Organizer 2:
   `Mods → Install From File → CompletePlanetSurvey.zip`
3. Launch via `sfse_loader.exe`
4. In game: `Settings → Gameplay → Complete Planet Survey → Auto-Complete Survey
   on Scan` → set `On`

---

## Architecture

Three layers, each doing one thing:

```text
ESM (CompletePlanetSurvey.esm) ─── tells the game engine to render the
                                   "Auto-Complete Survey on Scan" toggle in
                                   Settings → Gameplay
                ▲
                │ Game.GetFormFromFile(0x80C, ...)
                │ GameplayOption.GetValue()
                ▼
Papyrus (CompletePlanetSurveyQuest.psc) ─── glue: reads the toggle, calls
                                            native functions to mark survey
                                            data, dispatches the survey-complete
                                            engine event
                ▲
                │ DispatchStaticCall("CompleteSurveyIfEnabled")
                │ via SFSE messaging
                ▼
DLL (CompletePlanetSurvey.dll) ─── SFSE plugin: hooks the scan call site,
                                   binds Papyrus natives that write directly
                                   into the engine's knowledge DB
```

The ESM contains exactly two records authored in Creation Kit:

| Type | EditorID | FormID | Purpose |
| --- | --- | --- | --- |
| GPOG | `CPSGroup` | `0x080B` | Section header "Complete Planet Survey" in the menu |
| GPOF | `CPSScanAutoComplete` | `0x080C` | The actual toggle (checkbox, default off) |

The DLL hooks the `CALL` from `ID_52157` (planet-progress updater) to
`ID_97853` (survey notify) using `REL::GetTrampoline().write_call<5>`. The
call site is found at runtime by scanning the first `0x400` bytes of `ID_52157`
for an `E8 rel32` whose decoded target equals the address of `ID_97853`.

---

## What we took

| Dependency | What it gives us |
| --- | --- |
| **SFSE 0.2.19** | DLL injection, plugin loader, messaging interface, trampoline allocator |
| **CommonLibSF** | RE struct definitions (`PlayerCharacter`, `TESDataHandler`, `BSScript::*`), Papyrus binding helpers, Address Library wrapper |
| **Address Library** for 1.16.236.0 | Stable ID → offset mapping (`REL::ID(N).address()`) |
| **Champollion** | Decompiles `.pex` to `.psc` to generate Papyrus stubs (`GameplayOption.psc` etc.) |
| **Creation Kit** | Authoring the ESM (GPOG + GPOF + GOGL parent-child wiring) |
| **Ghidra** | Static analysis of `Starfield.exe` to find the engine functions below |

---

## What was missing from SFSE / CommonLibSF

None of the following were exposed by SFSE or CommonLibSF — we built them
ourselves from Ghidra analysis:

| Need | What we built |
| --- | --- |
| Read/write per-planet survey state | Direct write into `BSComponentDB2` slot arrays |
| Mark a single species "scanned" for a planet | `MarkSpeciesScannedForPlanet` via `ID_124898` |
| Mark a planet trait as known | `MarkTraitKnownForPlanet` via `ID_52155` |
| Sweep all known/required forms for a planet | `MarkEverythingForPlanet` via aggregator `ID_1016657` |
| Trigger the "Survey Complete" event + slate | Call into `ID_97853` after writing |
| Hook the "player scanned something" event | Call-site detour on `ID_52157 → ID_97853` |
| Make a Settings menu toggle from a third-party plugin | GPOG+GPOF wiring (no helpers exist; learned by reading official DLCs in xEdit) |

---

## Ghidra findings we actually use

All offsets and IDs are for Starfield 1.16.236.0. Address Library decouples
the IDs from the runtime offsets so they survive game patches.

### Knowledge database

Survey state lives in a `BSComponentDB2` component called
`BSGalaxy::PlayerKnowledge`, attached **per body** (not on
`BGSPlanet::PlanetData`, which is shared immutable data). Two discriminator
globals select the component family:

- `ID_938333` (uint16, `.bss`) — trait / per-planet-progress discriminator
- `ID_939118` (uint16, `.bss`) — per-reference scan-state discriminator

Both are populated at runtime during `BSComponentDB2::Detail::ComponentFactoryImpl_*`
registration; reading them before that returns `0`.

The knowledge manager singleton is `ID_126578()`. From there:

- `manager + 0x8B0` → DB pointer
- `db + 0x268` → `BSTHashMap` keyed on the 64-bit composite
  `(disc << 48) | (lower_id << 16)`
- Lookup via `ID_126806(container, out[4], &key)`:
  - `out[3] == 0xfe0` → miss
  - Otherwise, entry = `out[2] + *(uint16*)(out[2] + 0x12 + out[3] * 4)`

### Per-planet component value layout

The component value at `(disc=938333, key=planet_id)`:

```text
+0x00  uint64   header (slot count, etc.)
+0x18  ptr      slot array → 0x10-byte entries
   each entry:
     +0x00  uint32  form_id
     +0x04  uint32  flags (bit 0 = "known")
+0x20  ptr      "subobj" — secondary structure for per-species flag bytes
   subobj+0x20  → array used by ID_124898 to flip the scan byte at +0x21
```

The aggregator `ID_1016657` writes four arrays into the caller's buffer
(`>= 0x250` bytes; we allocate `0x400` for safety): two uint-arrays for flora
and trait IDs, two pointer-arrays for resources and other forms. We iterate
all four to feed `MarkEverythingForPlanet`.

### Engine functions we call

| ID | Signature | What it does |
| --- | --- | --- |
| `ID_126578` | `void*()` | Knowledge-manager singleton getter |
| `ID_126806` | `void*(container, out[4], &key)` | Generic `BSTHashMap` lookup |
| `ID_52155` | `void(planetId, BGSKeyword*, bool)` | `SetTraitKnown` — sets bit + dispatches event |
| `ID_52156` | `void(uint32_t* args, longlong* db)` | `AddOrRemoveKnownFormID` — appends/removes form_id in per-planet list |
| `ID_52157` | `void(ref, count, byte=0xd, byte, byte)` | Per-planet progress updater — **hook target** |
| `ID_83008` | `void(ref, scanned, byte=0xd, byte=0)` | `SetScanned` inner — dispatches to flora (`ID_83038`) or actor (`ID_52160`) |
| `ID_83038` | `void(db, args, &formId)` | Flora scan writer — flips `scanned` byte at `component+0x28` |
| `ID_97853` | `void(ctx)` | Survey-completion notify — generates Survey Data slate, **hook callee** |
| `ID_124898` | `void(subobj*, species_id, delta, 0)` | Per-species flag increment on subobj at `value + 0x20` |
| `ID_1016657` | `void(buffer, planet_id)` | Per-planet survey aggregator constructor |

### Hook path: from E-key to our code

```text
Player presses E to scan
  └─ Engine: scan dispatch
       └─ ID_83008 (SetScanned inner)
            ├─ flora path:  ID_83038 → (local_res8[0] != 0) → ID_52157
            └─ fauna path:  ID_52160 → ID_52157
                 └─ ID_52157 (planet-progress updater)
                      └─ CALL ID_97853 (survey notify)  ◄─── HOOK HERE
                           └─ our thunk:
                                ├─ call original ID_97853 first
                                └─ DispatchStaticCall("CompleteSurveyIfEnabled")
                                     └─ Papyrus reads GPOF, gates, runs marks
```

A single intercept point covers all scan paths because every path converges
at `ID_52157 → ID_97853`.

---

## Repo layout

```text
src/Main.cpp                                      # SFSE plugin
Data/CompletePlanetSurvey.esm                     # CK-authored toggle
Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc   # Papyrus glue
Data/Scripts/*.pex                                # Compiled scripts
fomod/                                            # FOMOD installer
extern/CommonLibSF/                               # SFSE/CommonLibSF (GPL-3.0)
re/                                               # Ghidra scripts + dumps
build.bat / deploy.bat / import-esm.bat / package.py     # Workflow scripts
```

---

## Build

Requires xmake, MSVC, Python 3 (for packaging), Starfield Creation Kit (only
if editing the ESM), and Papyrus Compiler from the CK install.

```bat
build.bat        :: compile DLL via xmake
deploy.bat       :: compile Papyrus, copy DLL+ESM+PEX to game, manage plugins.txt
import-esm.bat   :: copy game ESM back into the repo (run after editing in CK)
package.py       :: build the FOMOD-compatible CompletePlanetSurvey.zip for Vortex
```

Address Library (`offsets-1-16-236-0.txt`) is **not** checked in (19 MB) —
download from [Address Library on Nexus](https://www.nexusmods.com/starfield/mods/3256)
and place at the repo root before building.

---

## License

GPL-3.0 — see [LICENSE](LICENSE).

This mod links against [CommonLibSF](https://github.com/Starfield-Reverse-Engineering/CommonLibSF)
which is GPL-3.0 licensed; derived works (this mod) inherit GPL-3.0.

## Credits

- **SFSE** — script extender, author: ianpatt
- **CommonLibSF** — Bethesda RE library (GPL-3.0)
- **Address Library for Starfield** — author: meh321 (Nexus mod 3256)
- **Champollion** — Papyrus decompiler, used for stub generation
- **Ghidra** — National Security Agency, used for static analysis
