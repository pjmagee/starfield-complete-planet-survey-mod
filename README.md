# Complete Planet Survey

**Nexus:** <https://www.nexusmods.com/starfield/mods/16493>

A Starfield mod that completes a planet's survey — across every biome — the
moment you scan any single flora or fauna. Toggleable from **Settings →
Gameplay**.

Built as an SFSE plugin (DLL) + a tiny ESM (Settings toggle) + a Papyrus glue
script. Address Library makes the native IDs version-independent — see
"Ghidra findings" below. The game / SFSE versions each release was built and
tested against are recorded in [CHANGELOG.md](CHANGELOG.md).

---

## What it does

When you press E on any plant or creature (scanner up via F, or a single tap
to trigger a scan):

- Planet survey % → **100%**, the `<Planet> Survey Data` slate drops into
  your inventory — same engine event that fires after a vanilla full survey.
- **BIOME COMPLETE** label fires for every biome on the planet, not just
  the one you're standing in.
- Instant Scan per-species GMSTs are patched so natural scans also complete
  individual species in a single press (same effect as the separate
  [Instant Scan](https://www.nexusmods.com/starfield/mods/759) mod — built in).
- Nearby in-world refs (flora/fauna you can see in the scanner) refresh
  from blue "unscanned" to the scanned-colour outline.

`cgf "CompletePlanetSurveyQuest.CompleteSurvey"` from the console does the
same thing regardless of the toggle state.

---

## Install

1. Install [SFSE](https://www.nexusmods.com/starfield/mods/7589) and
   [Address Library](https://www.nexusmods.com/starfield/mods/3256).
2. Drop `CompletePlanetSurvey.zip` into Vortex / Mod Organizer 2 via
   `Mods → Install From File`. The archive is a flat `Data/`-prefixed layout
   (no FOMOD installer), which matches what popular Starfield SFSE mods ship
   and avoids a known MO2 install issue.
3. Launch via `sfse_loader.exe`.
4. In game: `Settings → Gameplay → Complete Planet Survey → Auto-Complete
   Survey on Scan` → **On**.

---

## How it works

Three layers. Each does one thing:

```text
ESM (CompletePlanetSurvey.esm) ─── a Settings-menu toggle record only.
                                   CK-authored GPOG + GPOF.
                ▲
                │ Game.GetFormFromFile(0x80C, ...) → GameplayOption.GetValue()
                ▼
Papyrus (CompletePlanetSurveyQuest.psc) ─── reads the toggle, orchestrates the
                                            four survey categories, calls DLL
                                            natives for the heavy lifting.
                ▲
                │ DispatchStaticCall("CompleteSurvey") from the poller.
                ▼
DLL (CompletePlanetSurvey.dll) ─── SFSE plugin: hooks the scan call site,
                                   binds the Papyrus natives, enumerates
                                   species, flips per-ref scan state, patches
                                   the Instant Scan GMSTs, runs a per-frame
                                   poller that gates deferred work.
```

### The core trick: spawn-and-scan per species

DB-level marks alone don't fire **BIOME COMPLETE** — the engine's per-biome
tracker only advances when a real `SetScanned` call fires on a ref of each
species. To cover every biome without physically visiting them:

1. Enumerate every flora + fauna species the planet tracks (engine aggregator
   `ID_1016657`, broader than Papyrus's per-biome `GetBiomeFlora`).
2. `PlaceAtMe` a ref of each species on the player — kept **initially disabled
   so invisible, no visual flicker**.
3. Call `SetScanned(true)` on each. That drives `ID_83008 → ID_52160` for
   fauna (works cleanly) or `ID_83008 → ID_83038` for flora. The flora path
   no-ops on `PlaceAtMe`'d refs because they lack the
   `(ID_939118, ref_formID)` component that `ID_83038` checks for.
4. For the flora gap, call `UpdatePlanetProgressForSpecies` — invokes
   `ID_52157` (the per-planet progress updater) directly, bypassing
   `ID_83038`'s component check.
5. `Disable + Delete` the spawned refs. Scan flags persist, refs don't.

### Why the dispatch is deferred

The scan hook fires *inside* the engine's scan call chain (`ID_52157 →
ID_97853`). Running `PlaceAtMe` for 20+ species from that context races
with the live scanner UI's ref-list rendering and crashes. So the hook only
sets a pending flag; an SFSE per-frame task (running between frames, outside
any engine call stack) picks up the flag and dispatches the actual
`CompleteSurvey` from a clean context.

### Instant Scan GMST patch

On plugin init we set `iHandScannerAnimalCountBase` and
`iHandScannerPlantsCountBase` to `1` via `RE::GameSettingCollection`. Each
individual scan now completes a species in one press — equivalent to the
dedicated [Instant Scan](https://www.nexusmods.com/starfield/mods/759) mod.

### The ESM

Two CK-authored records, wired into the existing Gameplay settings section:

| Type | EditorID              | FormID   | Purpose                                   |
| ---- | --------------------- | -------- | ----------------------------------------- |
| GPOG | `CPSGroup`            | `0x080B` | Section header in Settings → Gameplay     |
| GPOF | `CPSScanAutoComplete` | `0x080C` | The actual toggle (checkbox, default off) |

### The hook

`REL::GetTrampoline().write_call<5>` on the `CALL` from `ID_52157`
(planet-progress updater) to `ID_97853` (survey notify). The call site is
found at runtime by scanning the first `0x400` bytes of `ID_52157` for an
`E8 rel32` whose decoded target equals the address of `ID_97853`.

---

## Ghidra findings we actually use

The IDs below were Ghidra-derived on Starfield 1.16.236.0. Address Library
decouples these IDs from runtime offsets so the mod survives game patches
(as long as the IDs stay mapped); the game / SFSE versions each release was
built and tested against are recorded in [CHANGELOG.md](CHANGELOG.md).

### Knowledge database

Survey state lives in a `BSComponentDB2` component called
`BSGalaxy::PlayerKnowledge`, attached **per body** (not on
`BGSPlanet::PlanetData`, which is shared immutable data). Two discriminator
globals select the component family:

- `ID_938333` (uint16, `.bss`) — per-planet-progress + trait discriminator
- `ID_939118` (uint16, `.bss`) — per-reference scan-state discriminator.
  Attached by biome procedural generation; **absent on `PlaceAtMe`'d refs**,
  which is why `ID_83038` (flora writer) no-ops on them and we bypass to
  `ID_52157` directly.

The knowledge manager singleton is `ID_126578()`. From there:

- `manager + 0x8B0` → DB pointer
- `db + 0x268` → `BSTHashMap` keyed on the 64-bit composite
  `(disc << 48) | (lower_id << 16)`
- Lookup via `ID_126806(container, out[4], &key)`:
  `out[3] == 0xfe0` → miss; otherwise
  `entry = out[2] + *(uint16*)(out[2] + 0x12 + out[3] * 4)`.

### Per-planet component value layout

```text
+0x00  uint64   header (slot count, etc.)
+0x18  ptr      slot array → 0x10-byte entries
                  +0x00  uint32  form_id
                  +0x04  uint32  flags (bit 0 = "known")
+0x20  ptr      subobj — used by ID_124898 to flip the scan byte at +0x21
```

The aggregator `ID_1016657` populates a caller-provided buffer (`>= 0x250`
bytes; we allocate `0x400`) with four arrays of form IDs across all biomes:
two uint-arrays (inline IDs, e.g. traits) and two `TESForm*[]` arrays
(pointer indirection, form ID at `*ptr + 0x28`). Our `MarkResourcesForPlanet`
iterates all four and calls `IncrementScanFlag`; our `EnumeratePlanetSpecies`
filters to `FLOR` + `NPC_` types to produce the spawn-and-scan list.

### Engine functions we call

| ID         | Signature                                       | What it does                                                          |
| ---------- | ----------------------------------------------- | --------------------------------------------------------------------- |
| `126578`   | `void*()`                                       | Knowledge-manager singleton getter                                    |
| `126806`   | `void*(container, out[4], &key)`                | Generic `BSTHashMap` lookup                                           |
| `52155`    | `void(planetId, BGSKeyword*, bool)`             | `SetTraitKnown` — sets bit + dispatches trait-progress event          |
| `52157`    | `void(ref, count, byte=0xd, byte, byte)`        | Per-planet progress updater. Hook target; we also call it directly    |
| `83008`    | `void(ref, scanned, byte=0xd, byte=0)`          | `SetScanned` inner — dispatches to flora or actor writer              |
| `97853`    | `void(ctx)`                                     | Survey-completion notify. Hook callee; generates Survey Data slate    |
| `124898`   | `void(subobj*, species_id, delta, 0)`           | Per-species scan-flag increment on subobj at `value + 0x20`           |
| `1016657`  | `void(buffer, planet_id)`                       | Per-planet survey aggregator — enumerates all tracked forms           |
| `83007`    | `char(ref)`                                     | `IsBiomeRef` — returns 0/1/2 based on biome component presence        |
| `65318`    | `void(buffer)`                                  | Aggregator buffer cleanup                                             |

### Hook path: from E-key to our code

```text
Player presses E to scan (scanner UI up via F)
  └─ Engine scan dispatch
       └─ ID_83008 (SetScanned inner)
            ├─ flora path:  ID_83038 → (component exists) → ID_52157
            └─ fauna path:  ID_52160 → ID_52157
                 └─ ID_52157 (planet-progress updater)
                      └─ CALL ID_97853 (survey notify)  ◄─── HOOK HERE
                           └─ our thunk:
                                ├─ call original ID_97853 first
                                └─ DispatchStaticCall("CompleteSurveyIfEnabled")
                                     └─ Papyrus: read GPOF, gate on
                                                 planet < 100%, queue flag
                                                                │
                                                                ▼
                              (engine scan unwinds, frames advance)
                                                                │
                              SFSE per-frame task polls the flag,
                              dispatches CompleteSurvey from clean context,
                              spawns + scans + cleans up every species.
```

A single intercept point covers all scan paths because every path converges
at `ID_52157 → ID_97853`. Deferring via the poller avoids the active-scanner
race.

---

## Repo layout

```text
src/Main.cpp                                      # SFSE plugin
include/PCH.h                                     # precompiled header
Data/CompletePlanetSurvey.esm                     # CK-authored Settings toggle
Data/Scripts/Source/User/*.psc                    # Papyrus sources
Data/Scripts/*.pex                                # Compiled scripts
extern/CommonLibSF/                               # SFSE/CommonLibSF (GPL-3.0)
re/                                               # Ghidra scripts + output dumps
.clang-format / .clang-tidy                       # C++ style + lint
.vscode/settings.json                             # compile_commands wiring
build.bat / deploy.bat / import-esm.bat / package.py
```

---

## Build

Requires xmake, MSVC, Python 3 (for packaging), Starfield Creation Kit (only
if editing the ESM), and the Papyrus Compiler from the CK install.

```bat
build.bat        :: compile DLL via xmake
deploy.bat       :: compile Papyrus, copy DLL+ESM+PEX to game, manage plugins.txt
import-esm.bat   :: copy game ESM back into the repo (run after editing in CK)
package.py       :: build the distributable CompletePlanetSurvey.zip
```

Address Library (`offsets-1-16-236-0.txt`) is **not** checked in (19 MB) —
download from [Address Library on Nexus](https://www.nexusmods.com/starfield/mods/3256)
and place at the repo root before building.

For VSCode IntelliSense, generate `compile_commands.json` once after cloning:

```bat
xmake project -k compile_commands .
```

---

## License

GPL-3.0 — see [LICENSE](LICENSE).

This mod links against [CommonLibSF](https://github.com/Starfield-Reverse-Engineering/CommonLibSF)
which is GPL-3.0 licensed; derived works (this mod) inherit GPL-3.0.

## Credits

- **SFSE** — script extender, author: ianpatt
- **CommonLibSF** — Bethesda RE library (GPL-3.0)
- **Address Library for Starfield** — author: meh321 (Nexus mod 3256)
- **Instant Scan** (Nexus mod 759) — reference for the GMST patch approach
- **Champollion** — Papyrus decompiler, used for stub generation
- **Ghidra** — National Security Agency, used for static analysis
