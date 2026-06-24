# COMPLETE scan→materialize→DB→render→GREEN model — unified synthesis (2026-06-22)

This is the SYNTHESIS of 5 subsystem RE maps, reconciled against the in-game GROUND TRUTH
(`memory/re_green_outline.md` "FULL MECHANISM" + "SOLUTION VALIDATED IN-GAME" sections).
**In-game results outrank every decompile claim.** Where a subsystem map contradicts the
in-game arbiter, the in-game result wins and the contradiction is called out explicitly.

Confidence tags: `[in-game]` user-verified (authoritative) · `[decompile]` read from asm/C ·
`[inferred]` reasoned, not directly measured. Offsets byte-identical 1.16.236 ↔ 1.16.244.

---

## 0. THE ONE-PARAGRAPH ANSWER

A species renders **PROPER GREEN** (outline + info panel + XP) iff, in the persistent
`BSGalaxy::PlayerKnowledge` record at `db+0x268`, entry key `(ID_938333<<48)|(P<<16)`, the
per-(planet,species) **slot** (FNV-1a of the species id, `subobj+0x40 + idx*0x30`) has BOTH:
**(1)** `slot+0x21` (scan-flag byte) non-zero, AND **(2)** `slot+0x08` (a `BSTArray<uint32_t>`)
**non-empty and containing the species' correct ATTRIBUTE-CATEGORY marker form-ids**. Flora's
marker set is the universal-ish category set + per-species value markers; fauna's set leads with
a per-species **X** that is **materialization-bound** (no ref-free source in this build). The
planet key `P` must be the render domain id `ID_52188(player)` — which equals `*(planetForm+0x54)`
for the current planet `[in-game: 0x0003F5A1 == 0x0003F5A1]` but has **no proven off-planet map**
for a remote planet. **Net: flora green is fully ref-free on the current planet; fauna green needs
on-planet materialization (the live biome member read or CompleteSurvey); remote green of either
is blocked by the planet-key domain.**

---

## 1. UNIFIED DATA-FLOW MODEL (authored → materialize → DB → render)

```
 AUTHORED (ESM, ref-free)                 RUNTIME / MATERIALIZED (on-planet)         PERSISTENT DB (saved)
 ────────────────────────                 ──────────────────────────────────        ─────────────────────
 PNDT/PPBD per planet:                     biome = *(REL::ID(937609)+0x160)          db = *(ID_126578()+0x8B0)
   species list  ───────────┐             (NULL off-planet)                         container = db+0x268
   biome count   ──────────┐│             member table = biome+0x20                  entry key = (938333<<48)|(P<<16)
 FLOR/NPC_ base forms:      ││             (FNV-1a, 0x28 stride, ID_56887)               P = ID_52188(player)  [RENDER]
   form+0x2e type ('2'/'.') ││                member+0x08..0x10 = uint[] markers      subobj = entry + 0x20
   form+0x260 affinity (FAUNA)│            (built by ID_83025→ID_83024 on cell load)  species hashmap = subobj+0x18
 STATIC category forms:     ││                  ▲ ID_83024(catalog, species, &arr)        (FNV-1a ID_124901, key=species id)
   0023E90D Resource        ││                  │ catalog=909810(KYWD)/909812(other)   slot = *(subobj+0x40)+idx*0x30 (0x30)
   002634BE Biomes          ││                  │ reads species form + global catalog     slot+0x00  species id
   0023E90C Genetics        │└──────────┐       │ ONLY — no biome in body [decompile]     slot+0x08  BSTArray<u32> markers ◄── GREEN gate (2)
   00171867 flora-Repro     │           │       │ but FAULTS off a BARE form [in-game]    slot+0x10/+0x18  end/cap
   00171869 flora 5th       │           ▼       │                                         slot+0x20  percent  (ID_124899)
   002634C2 fauna-Repro     │   ID_52157(ref,species,…)                                   slot+0x21  scan-flag ◄── GREEN gate (1) (ID_124898)
   fauna X (per-species)    │     resolve P = ID_52188(ref)  [RENDER domain]              slot+0x24  survey dword
                            │     → ID_52158(ctx,&db):  THE WRITER                        slot+0x28  bucket next-link
                            │        • ID_124898 → slot+0x21 (saturating add)
                            │        • ID_124899 → slot+0x20 (percent)
                            │        • biome pass: ID_56887(biome+0x20)→member uint[]
                            │          → copy/dedup into slot+0x08 via ID_35755  ◄── fills GREEN gate (2)
                            │
                            └──────────────────────────────────────────────►  RENDER READ
                                                                              ID_90491/ID_90548 (outline decider):
                                                                                green = ID_52159(player, *(form+0x28)) ≠ 0
                                                                                        OR (species ∉ ID_52180 membership set)
                                                                              ID_52159 reads slot+0x21 for P=ID_52188(player)
                                                                              INFO PANEL / XP / "fully catalogued":
                                                                                ID_1016657→ID_97850→ID_97851 walk slot+0x08
                                                                                marker ids, each marker's own +0x21 ≥ threshold
```

### 1.1 The materialization chain, function by function

| Step | Function | Reads | Writes | authored/runtime |
|---|---|---|---|---|
| Papyrus SetScanned | `ID_118472` | ref | dispatches | runtime |
| component birth | `ID_83005`(create) / `ID_83038`(find) | ref, canonical | ScannableComponent +0x24/+0x28 | runtime (on a LIVE spawn only) |
| per-species progress | `ID_52157` | ref → `ID_52188(ref)`=P | builds ctx | runtime |
| **the writer** | **`ID_52158`** | ctx, `db`, live biome `937609+0x160` | slot +0x20/+0x21/+0x08 | runtime |
| flag add | `ID_124898` | subobj, species | slot+0x21 (sat. add); slot create moves staging array | — |
| percent | `ID_124899` | subobj, species | slot+0x20 | — |
| biome member find | `ID_56887` | biome+0x20, species key | — (locate member) | runtime |
| member (re)materialize | `ID_83025`→`ID_83024` | catalog(909810/812), species | member uint[] | runtime fill of authored values |
| array push | `ID_35755`/`ID_37867` | slot+0x08 hdr, id | slot+0x08 grow+append | — |

### 1.2 Per-datum: AUTHORED (ref-free) vs RUNTIME (on-planet)

| Datum | Verdict | Source / note |
|---|---|---|
| species list per planet | **AUTHORED** | PNDT/PPBD; `Esm::GetPlanetSpecies()` |
| biome count per (planet,species) | **AUTHORED** | PPBD occurrence count; `Esm::GetSpeciesBiomeCount()` |
| flora category-marker VALUES | **AUTHORED** (static forms) | 0023E90D/002634BE/0023E90C/00171867 (+00171869) |
| fauna shared-3 marker VALUES | **AUTHORED** (static forms) | 0023E90D/002634BE/002634C2 |
| **fauna X** (1st marker) | **RUNTIME** in this build | `form+0x260` is a SHARED STATIC off a bare form `[in-game]`; only the live biome member uint[][0] or CompleteSurvey yields it |
| flora per-species value markers | partly **RUNTIME** | the universal-4 greens only flora whose Resource/Genetics/Repro VALUES match; mismatched flora stay blue `[in-game, 5-flora split]` |
| slot+0x08 array (container) | **RUNTIME-filled** | only `ID_52158` biome pass fills it (reads live biome); contents are authored values |
| slot+0x21 / slot+0x20 | **RUNTIME but PERSISTENT** | saved in db+0x268; deterministic key |
| canonical id (`ID_83006`) | **AUTHORED in principle** | reads only static base-form fields; but FAULTS / returns 0 on bare forms `[in-game]`. Moot: `canonical==authored` for all probed species |
| planet key `P` (render) | **RUNTIME** | `ID_52188(player)`; == `+0x54` for current planet, unmapped for remote |
| catalog singletons 909810/909812 | **RUNTIME** | null in static exe; live after engine init |

---

## 2. THE DEFINITIVE GREEN PREDICATE (resolved)

> **PROPER GREEN(planet P, species S)  ⟺**
> in entry `(ID_938333<<48)|(P<<16)`, slot = FNV-1a(S) in `subobj+0x18`:
> **`slot+0x21 ≠ 0`  AND  `slot+0x08` BSTArray<u32> is NON-EMPTY with S's correct marker set**,
> **AND** `P == ID_52188(player)` (the render-domain id for the planet being looked at).

### 2.1 Reconciliation of the central contradiction (slot+0x08 vs +0x21)

Two of the five subsystem maps inherited the **stale** `slot-0x08-catalogue-writer-2026-06-22.md`
model, which asserts:
- "green = `slot+0x21` ALONE (`ID_52159` reads raw +0x21, nonzero ⇒ green)"; and
- "slot+0x08 = co-biome **SPECIES** form-ids, info-panel only."

**Both are DISPROVEN in-game and must not be trusted:**
1. `[in-game]` `TestDirectGreen` wrote `+0x21` for 17 species (canonical==authored key, correct
   planet key) → SAVE → reload → **STILL BLUE** while survey-% read 100%. So `+0x21` alone is
   **not** the gate. `ProbeRenderRead` (calling `ID_52159` directly) returned nonzero 17/17 for the
   poke yet the outline stayed blue — i.e. `ID_52159`'s `+0x21` read is the **half-scan / %** signal,
   not the outline gate.
2. `[in-game]` `DumpSpeciesSlots` byte-diff: after a real scan `slot+0x08` is a POPULATED
   `BSTArray<u32>`; after the poke it is NULL. Pushing the correct marker ids into `slot+0x08` via
   `PushSpeciesAttr` (engine `ID_35755`) made flora render **proper green** (outline + full info),
   live, no spawn. Pushing `0023E90D`+`002634BE` alone unlocked only "Resource"+"Biomes" rows and
   stayed blue; adding `0023E90C`+`00171867` → green. So **the gate is slot+0x08 non-empty/correct**.
3. `[in-game]` slot+0x08 contents are **ATTRIBUTE-CATEGORY marker forms** (0x00xxxxxx statics),
   heavily shared/deduped across species — **NOT** co-biome species ids (the catalogue-writer doc's
   element-type claim is wrong). None of the values are the planet's species ids.

So `ID_52159`/`+0x21` is necessary (the scanned/% flag) but **not sufficient**; the engine outline
decider's true gate is the **non-empty correct slot+0x08 marker array** on the same slot. The exact
decompiled render read-site that consults slot+0x08 was **not** isolated (the decided gap below), but
the in-game arbiter is unambiguous and overrides the decompile. `[in-game]` outranks `[decompile]`.

### 2.2 Why the catalogue-writer doc looked right

`ID_52159` genuinely reads only `+0x21` — but `ID_52159` feeds the **survey-% / membership term**,
not the final outline color. The outline decider (`ID_90491`/`ID_90548`) additionally requires the
catalogued marker set; that requirement is what the in-game test exposed and the single-function
`ID_52159` trace missed. Treat `ID_52159` as the "% scanned" reader, NOT "the green reader".

---

## 3. BUILD RECIPE PER KINGDOM

### 3.1 FLORA — ref-free on the current planet (PROVEN), per-species values NOT universal

**Confirmed in-game:** the slot+0x08 set is `[Resource, Biomes, Genetics, Reproduction]` =
`[0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867]`, plus a 5th `0x00171869` on some flora.

**CRITICAL correction to the task brief's assumption:** the brief states the flora set is universal
with the 5th gated by biomes≥3 (BROADLEAF ROSE 2-biome green / TUFTED SNOW WILLOW 3-biome blue). The
**later** in-game pass (`re_green_outline.md` "FLORA MARKERS ARE VALUE-SPECIFIC… CORRECTED") **rules
the biome-count rule OUT**: with the universal-4 build, 2-biome flora SPLIT (BROADLEAF ROSE green,
COLD CAVE NETTLE blue) and 3-biome flora SPLIT (BOREAS ROOT green, TUFTED SNOW WILLOW blue) — so
biome-count is **not** the determinant. The determinant is the **Resource VALUE**: Nutrient/Fiber
green with `0x0023E90D`, Toxin/Metabolic-Agent stay blue → the slot[0] "Resource" marker is
resource-value-specific. **The universal-4 hardcode greens only flora whose values match the dumped
set. `EsmReader::GetSpeciesBiomeCount`'s ≥3 rule must NOT be deployed — it is wrong.**

→ **FLORA verdict:** the 4 (or 5) marker VALUES are authored statics, but WHICH ones a given flora
gets is per-species (resource/genetics/reproduction value), selected by the catalog conditions. The
fully-general flora set therefore needs the same per-species selector the fauna-X problem needs (the
live member uint[] / on-visit cache), NOT a hardcoded list. Hardcoding the universal-4 is a partial
solution that greens the majority (shared Genetics/Reproduction) but mis-greens value-divergent flora.

### 3.2 FAUNA — the DEFINITIVE X verdict

Fauna green set = `[X, 0x0023E90D, 0x002634BE, 0x002634C2]`, X per-species (observed values:
`0x00280178, 0x002634AE, 0x002634AD, 0x001699B2, 0x00280172`).

**Is there ANY ref-free path to X? NO (in this build).** Reconciling all routes against `[in-game]`:

| Route | Verdict |
|---|---|
| `GetFaunaX` direct affinity walk (`form+0x260`→+0x120→+0x238→type-0x9F) | **FAILS off-planet.** `form+0x260` is a SHARED STATIC (atype `0x00` not `'?'`, identical `0x7FF6852656D0` for all 9 fauna) on a bare form → returns 0 `[in-game]` |
| `ID_83024(catalog, bareForm, &arr)` direct | **FAULTS ×17 in-game.** Appends Resource+Biomes then AVs on the 3rd (per-species) condition — the condition VM (`ID_71429` cases 4/6/0x0b/0x0c) derefs the un-materialized `species[0x19]`/`+0x260` container. The "context-nulled ⇒ safe" decompile argument (subsystem map A.3 / the (A)-derivation doc) is **REFUTED**: nulling the TLS run-on context does NOT null the SUBJECT (the species form), and the species-property cases deref its un-materialized component container `[in-game ×17]` |
| `ID_83006(bareForm)` canonical | returns 0 / AV on bare forms `[in-game]` (NO-CANON ×17) |
| static fauna→X table | works but needs a full `DumpSpeciesSlots` sweep of the fauna roster (~6 X values seen, not exhaustive) |
| ESM NPC_ affinity parse in `EsmReader` | possible in principle, **not prototyped**; the authored 0x9F category entry would have to be parsed from the NPC_ component record |

**DEFINITIVE: fauna X is materialization-bound. There is NO reliable pure-ESM/ref-free read in this
build.** The two practical sources are **(C) the on-planet live biome member uint[][0]** or **a static
fauna→X table** harvested from it.

#### 3.2a On-planet fauna green WITHOUT spawning — the member-read recipe

`GreenPlanetProper` can green fauna on the CURRENT planet without spawning by reading X (and the
whole correct marker set) from the live biome member table, then pushing into slot+0x08. This is the
engine's own source array — strictly more correct than any hardcode.

`ID_56887` signature (`REL::ID(56887)` @ 0x140905bf0) — decompile-confirmed:
```c
//  longlong* ID_56887(longlong table /* = biome+0x20 */, longlong out[2], unsigned char* key4)
//  FNV-1a (seed 0xcbf29ce484222325, prime 0x100000001b3), 0x28 stride, 4-byte key.
//  out[0] = table base ; out[1] = matched index (== bucketCount on MISS).
//  member = *(table+0x00) + out[1]*0x28 ; member uint[] = [*(member+0x08) .. *(member+0x10)).
```

Guarded C++ (behind the existing `GuardedNative` /EHa + degraded latch). NB: `ID_56887` reads the
table at `*(param_1+0x20)` (bucket base) and `*(param_1+0x28)` (bucket count) — relative to the
`biome+0x20` pointer you pass, so out[0] == biome+0x20 and the base/count are at out[0]+0x20 / +0x28:
```cpp
namespace Engine {
inline REL::Relocation<std::uintptr_t*> Singleton937609 {REL::ID(937609)};
using fn_find_member_t = std::int64_t* (*)(std::int64_t table, std::int64_t out[2], unsigned char* key4);
inline REL::Relocation<fn_find_member_t> FindBiomeMember {REL::ID(56887)};

// Read a (current-planet, species) marker set from the LIVE biome member table.
// Returns false off-planet / on miss / on fault (caller leaves species blue). No alloc, pure loads.
bool ReadLiveMemberMarkers(std::uint32_t speciesFormId, std::vector<std::uint32_t>& out)
{
    out.clear();
    try {
        auto* s = Singleton937609.get();
        if (!s || !*s) return false;
        const auto biome = *reinterpret_cast<std::uintptr_t*>(*s + 0x160);
        if (!biome) return false;                       // off-planet / no live biome
        std::int64_t res[2] = {0, 0};
        unsigned char key[4]; std::memcpy(key, &speciesFormId, 4);
        FindBiomeMember(static_cast<std::int64_t>(biome) + 0x20, res, key);
        const auto table       = static_cast<std::uintptr_t>(res[0]);          // == biome+0x20
        const auto bucketBase  = *reinterpret_cast<std::uintptr_t*>(table + 0x20);
        const auto bucketCount = *reinterpret_cast<std::uint64_t*>(table + 0x28);
        const auto idx         = static_cast<std::uint64_t>(res[1]);
        if (bucketCount == 0 || idx == bucketCount || !bucketBase) return false; // empty / MISS
        const auto member = bucketBase + idx * 0x28;
        auto* begin = *reinterpret_cast<std::uint32_t**>(member + 0x08);
        auto* end   = *reinterpret_cast<std::uint32_t**>(member + 0x10);
        if (!begin || end < begin) return false;
        for (auto* p = begin; p != end; ++p) out.push_back(*p);                 // [X, Resource, Biomes, fauna-Repro]
        return !out.empty();
    } catch (...) { return false; }
}
} // namespace Engine
```
Then `for (id : out) PushSpeciesAttr(slotAddr, id);` + `IncrementScanFlag` for `+0x21`. This is the
recommended fauna path on the current planet: it sources X from the engine's own materialized array,
so it is correct for every fauna with no per-species table to maintain.

#### 3.2b Is REMOTE fauna green possible at all?

**No.** Two independent walls: (i) X has no off-planet source (3.2 table); (ii) even with the markers,
the render reads the entry keyed by `ID_52188(player)` and no `+0x54 → P_render` map exists off-planet
(§4). A remote planet has neither a live biome (no member table) nor a render-domain key. Remote fauna
green is **not achievable** in this build.

### 3.3 Summary table

| Kingdom | current planet | remote planet |
|---|---|---|
| FLORA | **ref-free** (member-read or value-correct set); hardcoded-4 greens the majority | **blocked** by planet-key domain (§4) even though markers are derivable |
| FAUNA | **on-planet only** via live member-read (3.2a) or CompleteSurvey | **NOT achievable** (X + key both materialization-bound) |

---

## 4. THE PLANET-KEY DOMAIN (the remote blocker, reconciled)

The render reads green from the entry keyed by `ID_52188(player)` (a runtime BSGalaxy NumericID from
the player's ExtraLocation-0x81 `node+0x28`), while the mod's ref-free writer + survey-% reader key by
`*(planetForm+0x54)` (FormID domain).

- `[in-game]` `TestRenderKeyGreen`: `formId(+0x54)=0x0003F5A1  renderId(ID_52188)=0x0003F5A1` — **SAME**
  for the current planet. So for the body you stand on, `+0x54` IS the render key (the
  `render-read-target` H2 "wrong planet key" verdict was DISPROVEN by the game for the current planet).
- For a **remote/never-visited** planet, no proven `+0x54 → P_render` converter exists off-planet
  (suspected in the `ID_51760`/`ID_51773`/`ID_124846`/`ID_42691` BSResource2 cluster, not decompiled to
  a closed form). So remote writes land in an entry the renderer may never read.

**Conclusion:** current-planet green can use `ID_52188(player)` (== `+0x54`) safely. Remote green is
NOT reliably achievable until the `+0x54 → P_render` map is found (and even then fauna-X remains
materialization-bound). This matches the user directive: pursue real ref-free/remote solutions, do
NOT auto-run CompleteSurvey on landing.

---

## 5. CONCRETE CODE CHANGES for `src/Main.cpp`

The mod already binds the write path correctly. The remaining work is the **slot+0x08 marker build**
and choosing per-species sources. Recommended changes, in priority order:

1. **Bind `ID_56887` and add `ReadLiveMemberMarkers`** (§3.2a). This is the single highest-value
   addition: it sources the engine-exact per-species marker set (flora value-markers AND fauna-X)
   from the live biome on the current planet, with no fault risk (pure loads, no condition VM).

2. **Add `GreenSpeciesProper(planetId, speciesFormId, slotAddr)`** that:
   - resolves `slotAddr` via `ResolvePlanetSubobj` + `SpeciesSlotHash` (already present);
   - `IncrementScanFlag(subobj, species, 100)` + `SetPercentByte(...)` → slot+0x21/+0x20;
   - obtains the marker set: try `ReadLiveMemberMarkers(species, markers)`; if empty and the species
     is flora, fall back to the universal-4 `[0x0023E90D,0x002634BE,0x0023E90C,0x00171867]` (greens the
     shared-value majority); for fauna with no member read, **leave blue** (do not hardcode a wrong X);
   - `for (id : markers) PushSpeciesAttr(slotAddr, id);` (engine-owned alloc, teardown-safe).

3. **`GreenPlanetProper(planetId)`** (current planet): for each `Esm::GetPlanetSpecies()[planetId]`
   species, call `GreenSpeciesProper` using `planetId = ID_52188(player)` (NOT `+0x54`). This greens
   flora AND fauna on the current planet ref-free (no spawn) — beats CompleteSurvey.

4. **REMOVE / demote the dead paths** (they chase non-problems and one of them faults):
   - `GetFaunaX` (form+0x260) — returns 0 off-planet; superseded by `ReadLiveMemberMarkers`. Keep
     only if you build a static fauna→X table from its on-planet output.
   - `BuildSpeciesMarkers`/`ID_83024` direct call — **FAULTS ×17 in-game**; do NOT ship it on bare
     forms. (`perspecies-marker-derivation`'s "(A) is ref-free safe" optimism is refuted by the game.)
   - `CanonicalFormId`/`ID_83006`/`ID_83009` — `canonical==authored` for all probed species; the
     canonical machinery is a non-problem and can be simplified to the authored id.
   - `GetSpeciesBiomeCount ≥3 → 5th marker` rule — **wrong** (biome-count disproven). Keep the util,
     do not use the rule.

5. **Galaxy sweep stays barren-only.** `CompleteAllPlanetsSurveyData_Phase1` already skips living
   worlds (`planetSpecies.count(planetId)`); keep that. A future `CompleteAllPlanets` that greens
   living worlds remotely is blocked by §4 + fauna-X and should not ship until the `+0x54→P_render`
   map is found.

### 5.1 Functions/offsets to bind (not yet in Main.cpp)

| Bind | REL::ID | Use |
|---|---|---|
| `FindBiomeMember` | `56887` | live biome member-read (§3.2a) |
| `Singleton937609` | `937609` | `+0x160` live biome, `+0x80` current planet id |
| (already bound) `PushSpeciesAttr`/`BSTArrayU32Grow` | `35755` | slot+0x08 push |
| (already bound) `ResolvePlanetSubobj`/`SpeciesSlotHash`/`IncrementScanFlag`/`SetPercentByte` | `126806/124901/124898/124899` | slot resolve + +0x21/+0x20 |

Key offsets (all 236==244): biome `+0x160` (live) / `+0x80` (planet id) off `*937609`; member table
`biome+0x20`; member `+0x08/+0x10/+0x18` uint[]; slot stride `0x30`, `+0x08` markers / `+0x20` pct /
`+0x21` flag; subobj species hashmap `+0x18`, slots `+0x40`; entry key `(938333<<48)|(P<<16)`.

---

## 6. OPEN GAPS (unproven; need a test or one-at-a-time RE)

1. **The decompiled render read-site that consults slot+0x08.** In-game proves slot+0x08-non-empty
   is required for green, but no single decompile shows the outline decider reading slot+0x08 (the
   `ID_90491`/`ID_90548`→`ID_52159` trace only shows the +0x21 read, which is the %/membership term).
   The catalogued-set check is most likely the `ID_1016657→ID_97850→ID_97851` per-marker count feeding
   a "fully catalogued" sub-term of the decider. Worth isolating to confirm the exact predicate, but
   the build recipe does not depend on it.
2. **`+0x54 → P_render` map** for remote green (§4). The one blocker for remote.
3. **The fully-general flora marker source** (per-species value markers). `ReadLiveMemberMarkers`
   solves it on-planet; a ref-free remote source (resource-value→marker map from PPBD RSGD/FLOR) is
   TBD and the hardcoded-4 is only a partial.
4. **Does fauna green with just the shared-3 (X for info only) or REQUIRE X?** In-game evidence leans
   "requires X" (slot[0] value-specific like flora's Resource). Confirm with `GreenPlanetProper` on a
   fauna planet using shared-3-only vs member-read.
5. **Persistence of a pure hand-built slot+0x08 across quit/reload** in open wilderness — one
   confirming save-test (settlements no-op).

---

## 7. WHAT IS SETTLED (do not re-derive)

- GREEN gate = `slot+0x21 ≠ 0` AND non-empty correct `slot+0x08` attribute-marker array, same slot,
  keyed by (authored==canonical) species id, in the `ID_52188(player)`-domain entry. `[in-game]`
- Flora slot+0x08 build via `PushSpeciesAttr` → proper green, live, ref-free, no spawn. `[in-game]`
- `ID_83024` ref-free on bare forms FAULTS; `GetFaunaX`/`form+0x260` is a shared static off-planet;
  fauna-X is materialization-bound. `[in-game]`
- biome-count→5th-marker rule is WRONG; flora markers are value-specific. `[in-game]`
- current-planet `+0x54 == ID_52188(player)`; remote `+0x54→P_render` unknown. `[in-game]`
- `canonical == authored` for all probed species; canonical machinery is a non-problem. `[in-game]`
</content>
</invoke>
