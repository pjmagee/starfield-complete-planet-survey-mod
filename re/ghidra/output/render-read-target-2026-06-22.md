# Render read-target — where the outline ACTUALLY reads green (2026-06-22)

Resolves H1 (wrong structure) vs H2 (wrong planet key) for the "TestDirectGreen wrote
+0x21 for 17 species under the authored==canonical species id at (938333|ReadPlanetId),
SAVE+quit+reload → survey % = 100% / BIOME COMPLETE, but the scanner OUTLINE stays BLUE"
ground truth. Evidence is fresh decompile (1.16.236 project; offsets byte-identical on
1.16.244 per offset-skew-236-vs-244.md) cross-checked against the in-game arbiter.

Confidence tags: `[in-game]` outranks all; `[decompile]` read directly; `[inferred]`.

---

## VERDICT: **H2 (WRONG PLANET KEY). H1 is DISPROVEN.**

The outline renderer reads the **SAME `+0x21` byte, in the SAME slot, by the SAME FNV
hash of the SAME (authored) species id** that the mod writes and that the survey-% reader
reads. There is **no second structure** (no tree, no different field) gating the persistent
biome green. What differs is the **planet key of the PlayerKnowledge entry**:

- The mod's write + the survey-% reader key the entry by **`(ID_938333 | *(planetForm+0x54))`**
  — the planet **form's stored id** (FormID-domain; what `ReadPlanetId` returns and what the
  ESM species map is keyed on).
- The outline renderer keys the entry by **`(ID_938333 | ID_52188(player))`** — a **runtime
  BSGalaxy planet NumericID** pulled from the player's ExtraLocation-0x81 node (`node+0x28`),
  with fallbacks to the parent-cell walk and the `ID_937609+0x80` current-planet global.

These are **two different planet-id domains.** They coincide for a *real scan* (both the
write and the read go through `ID_52188`), which is why on-planet `CompleteSurvey` greens.
They do **not** coincide for the mod's `+0x54`-keyed direct write, so the renderer looks up a
**different entry** (one with no `+0x21` for those species → 0 → blue), while the %-reader —
keyed by the same `+0x54` as the write — reads the mod's 100%. That is the exact "100% but
blue" signature. `[in-game]` arbiter + `[decompile]` mechanism.

### Why H1 (wrong structure) is dead

`ID_52159` (the green reader, species-scanned-check.txt:86-156 / q2-membership-key.txt:1-71)
and `ID_52158`/`ID_124898` (the real + mod writers) hit the **identical physical arrays**:

| Path | subobj base | species hashmap | slot array base | green byte |
|---|---|---|---|---|
| **reader** `ID_52159` | `R = entry + bucketOff` | `R+0x38` (`ID_124901`) | `*(R+0x60)` | `+0x21 + idx*0x30` |
| **real writer** `ID_52158` | `S = entry + bucketOff + 0x20` | `S+0x18` | `*(S+0x40)` | `+0x21 + idx*0x30` |
| **mod writer** `ID_124898` (via `ResolvePlanetSubobj`) | `S = entry + bucketOff + 0x20` | `S+0x18` | `*(S+0x40)` | `+0x21 + idx*0x30` |

`R = S - 0x20`, so `R+0x38 == S+0x18`, `R+0x60 == S+0x40`, `R+0x68 == S+0x48`. **All three
address the same hashmap and the same slot array; all read/write `+0x21` at the same
`idx*0x30` slot; all hash the bare 4-byte species id with the same FNV-1a (`ID_124901`).**
Citations: species-scanned-check.txt:46-50 (reader); real-scan-chain.txt:174-184 (real
writer's `lVar26 = entry + bucketOff + 0x20`, `ID_124901(lVar26+0x18)`, byte at
`*(lVar26+0x40)+0x21`); real-scan-chain.txt:557-628 (`ID_124898`); src/Main.cpp:266-270
(`ResolvePlanetSubobj` `entry + bucketOff + 0x20`). `[decompile]`

So if the planet key matched, `ID_52159` would read the mod's `+0x21 = 100` and return
nonzero → green. It returns blue → the key does **not** match. H1 cannot be the cause; the
species-key sub-variant is also excluded (probe already proved authored==canonical for all
17 species, re_green_outline.md "PROBE RESULT 2026-06-22").

### The membership branch (`ID_52180`) is a real second lever but NOT the cause here

`ID_90491`/`ID_90548` compute green = `(ID_52159 nonzero) OR (species ABSENT from the
ID_52180 planet membership set)` (q2-membership-key.txt:105-118; outline-decider.txt:428-446).
For a tracked biome species the species IS present in the set, so Term-2 is false and the
color is decided solely by Term-1 (`ID_52159`'s `+0x21` read). The membership set is keyed by
discriminator `ID_938158` off the **player's resolved planet** (`ID_47749`/`ID_38719`,
overnight-deep-re Q2) — i.e. the *same* `ID_52188`-domain planet, not `+0x54`. It does not
rescue the `+0x54` write. `[decompile]`

---

## THE RIGHT PLACE the renderer reads green from

> The `+0x21` scan-flag byte in the **BSGalaxy::PlayerKnowledge** record at `db+0x268`,
> entry key **`(ID_938333 << 48) | (P_render << 16)`** where **`P_render = ID_52188(player)`**
> (runtime planet NumericID), species slot via FNV-1a (`ID_124901`) of the **authored**
> species id, slot array `*(subobj+0x40)` at `+0x21 + idx*0x30`.

The structure is correct in the mod. **Only the planet-key domain is wrong.** The fix must
land `+0x21` in the entry keyed by `P_render`, not by `*(planetForm+0x54)`.

### What P_render is (decompile-exact)

`ID_52188(player, &outPlanet, &outSystem)` (planet-resolver.txt:1-96), in order:
1. `lVar2 = ID_37878(*(player+0xC8), 0x81)` — the player's ExtraLocation (type 0x81) node.
   If present and `*(int*)(node+0x28) != 0`: `outPlanet = *(int*)(node+0x28)`. `[decompile]`
2. else `ID_56990(player)` — parent-cell / surface walk → an id, with the
   `ID_937609+0x80` current-planet global as the terminal fallback
   (resolve-planet.txt:196-217). `[decompile]`
3. The produced id is compared against `*(int*)(ID_937609+0x80)` (the current-planet-id
   global) — confirming it is the **runtime BSGalaxy planet NumericID** domain, NOT a TESForm
   FormID and NOT `planetForm+0x54` (canonical-source.txt:112). `[decompile]`

`*(planetForm+0x54)` is the id the engine's *form-keyed* survey path uses (knowledge-api.txt:1176
keys `(938333 | *(param_2+0x54))`; the mod's `ReadPlanetId`, the ESM map key, `ID_102650`/
`ID_102651` data sweep, and `ID_97851`/`GetSurveyPercent` all live in this domain). The render
path is the **only** consumer in the *runtime-`ID_52188`* domain. Whether the two numeric
values are equal for a given planet is **not** guaranteed by anything in the decompile, and the
in-game result proves they are **not** equal (else green). `[in-game]` + `[decompile]`

### Why on-planet CompleteSurvey greens (control case)

`CompleteSurvey` → `SpawnAndScanAllPlanetSpecies` → per live spawned ref: `SetScanned(true)`
(`ID_83008`) + `UpdatePlanetProgressForSpecies` → `ID_52157(ref, baseFid, 0xd, 0, 0)`
(CompletePlanetSurveyQuest.psc:291-301; src/Main.cpp:180-185). `ID_52157` resolves the write
planet via **`ID_52188(ref)`** (planet-resolver.txt:142) → the same runtime `P_render` the
player stands on → writes `+0x21` into the **`P_render`-keyed** entry. The renderer reads the
`P_render`-keyed entry → finds `+0x21` → **green, persists.** The write and read share the
`ID_52188` domain; that is the whole reason it works and `+0x54` does not. `[decompile]` +
`[in-game]` (G1/G2)

---

## THE FIX

The renderer's entry is keyed by `ID_52188(player)`. To green ref-free, `+0x21` must land in
**that** entry. Two routes, in order of reliability:

### FIX A (preferred, ref-free, explicit target) — write `+0x21` under the ID_52188 NumericID, not `+0x54`

Make `ResolvePlanetSubobj` key by the **render-domain planet id** for the planet being greened,
not `*(planetForm+0x54)`. Concretely, obtain `P_render` for a target planet and write `+0x21`
under `(938333 | P_render)`:

- **On the planet you stand on** (the only case TestDirectGreen actually tests): call
  `ID_52188(player, &p, &sys)` once, then `MarkSpeciesScannedForPlanet(p, authoredFid, 100)`.
  This writes into the exact entry the renderer reads → predicted **green on reload**, no spawn,
  no `ID_52158`. This is the minimal, decisive change to validate the whole verdict in one run.
  `[inferred]` from the decompile; needs the save-test to confirm.
- **For a remote/never-visited planet**, you need that planet's runtime NumericID
  (`P_render`) without `ID_52188(player)` (which only yields the *current* planet). Candidates,
  to be resolved by RE before relying on them:
  - The id produced by `ID_102650`/`ID_102651` when the data sweep creates the entry — i.e.
    **which domain the data-sweep entry is actually keyed in.** If `ID_102650(0, X, 1)` is
    called with `X = *(planetForm+0x54)`, the sweep's entry is `+0x54`-keyed and the render
    will *still* miss it — meaning even the data sweep's entries are in the wrong domain for
    green, and a render-domain id must be derived per planet (e.g. via the BSGalaxy/BSResource
    catalog `ID_51760`/`ID_51773`/`ID_124846` seen inside `ID_56990`/`ID_52188`).
  - **OPEN, must-resolve:** the map `*(planetForm+0x54)` (FormID-domain) → `P_render`
    (BSGalaxy NumericID-domain). Until that mapping is known, remote ref-free green is **not**
    safely achievable; only *current-planet* ref-free green (the bullet above) is.

### FIX B (proven shape, current planet) — drive the engine writer per explicit planet with P_render

The mod already has `CompleteTypeForPlanet`/`GreenAndCompleteTypeForPlanet` driving `ID_52158`
directly (src/Main.cpp:340-373). Feeding it `planetId = ID_52188(player)` (NOT `+0x54`) plus a
live spawned ref greens the current planet through the engine's own writer (full propagation +
events), landing `+0x21` in the `P_render` entry. This is essentially what `CompleteSurvey`
already does via `ID_52157`; the only change is making the planet id explicit and in the render
domain. For **remote** planets it has the same blocker as FIX A: `ID_52158` keys by the ctx
`planetId` you pass, so you still need the target's `P_render`, and its biome-propagation half
reads the *current* biome (`ID_937609+0x160`) so it partially no-ops off-planet. `[decompile]`

### Bottom line on remote green

If the only obtainable planet id for a remote body is `*(planetForm+0x54)` (FormID-domain) and
no `+0x54 → P_render` mapping exists off-planet, then **persistent remote green is NOT reliably
achievable ref-free** — the renderer will never read a `+0x54`-keyed entry. The robust,
confirmed-shape path remains **on-planet** (`ID_52188(player)` gives the correct `P_render`
for the body you stand on). The single cheapest confirmation of this entire verdict is the
FIX-A current-planet test: `MarkSpeciesScannedForPlanet(ID_52188(player), authoredFid, 100)`
→ SAVE → quit → reload → expect **green**. If that greens where `+0x54` did not, H2 is proven
in-game and the remaining work is purely the `+0x54 → P_render` derivation for remote bodies.

---

## IDs I could NOT fully resolve

1. **The numeric relationship `*(planetForm+0x54)` ↔ `ID_52188(player)`** for a *given* planet
   — i.e. are they ever equal, and what function maps FormID-domain → BSGalaxy NumericID. The
   decompile shows they are produced by disjoint mechanisms (form-field read vs runtime
   ExtraLocation/catalog resolve) and the in-game blue proves they differ for the tested planet,
   but the exact converter (likely in the `ID_51760`/`ID_51773`/`ID_124846`/`ID_42691` BSResource2
   catalog cluster reached from `ID_56990`/`ID_52188`) was not decompiled to a closed form. This
   is the one gap blocking *remote* (vs current-planet) ref-free green.
2. **What `ID_102650`/`ID_102651` key their created entry in** (FormID `+0x54` vs `P_render`).
   `ID_102651` keys `(938333 | *param_2)` where `*param_2` is the caller's passed id
   (scan-complete.txt:73-90); the mod passes `*(planetForm+0x54)` (src/Main.cpp:656). So the
   sweep's entries are almost certainly `+0x54`-keyed — meaning they are read by %/UI but **not**
   by the render. Worth a one-line confirm before building FIX-A remote on top of the sweep.
3. **`node+0x28` provenance** — confirmed as the value `ID_52188` returns and as the
   render/membership planet key, but whether the engine stamps it from the planet's FormID or
   from a separate BSGalaxy NumericID at materialization time is unproven (it is compared to the
   `ID_937609+0x80` global, which is the NumericID domain, so the latter is most likely).
