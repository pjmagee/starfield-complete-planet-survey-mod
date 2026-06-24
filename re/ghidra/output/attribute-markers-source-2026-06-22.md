# slot+0x08 attribute markers — what they are, where they come from, ref-free verdict (2026-06-22)

**Validated in-game premise (authoritative, supersedes the earlier "co-biome sibling species ids" guess).**
The per-(planet,species) survey slot's `slot+0x08` `BSTArray<uint32_t>` holds **attribute-CATEGORY
marker form-ids**. Pushing the full set greens the species (outline + info panel + XP). Observed:
- **FLORA (universal):** `[0x0023E90D Resource, 0x002634BE Biomes, 0x0023E90C Genetics, 0x00171867 Reproduction]`.
  5-element flora inserts a 2nd biome/genetics marker `0x00171869`.
- **FAUNA:** `[X, 0x0023E90D Resource, 0x002634BE Biomes, 0x002634C2 <fauna-Reproduction/Temperament>]`,
  where **X is species-specific** (observed: `0x002634AE, 0x002634AD, 0x00280178, 0x001699B2, 0x00280172`).

This pass decompiles the engine path that fills `slot+0x08` to answer (1) the biome-member source,
(2) what the markers ARE (record type), (3) the fauna-X source, (4) the ref-free verdict + recipe.
Raw dumps: `re/ghidra/output/attr-marker-source-raw.txt`, `attr-marker-source-raw2.txt` (1.16.236
Ghidra project; offsets confirmed identical on 1.16.244).

---

## TL;DR (the four answers)

1. **Biome-member source.** `ID_52158`'s biome pass reads `biome = *(ID_937609+0x160)` (the LIVE current
   biome), `ID_56887(biome+0x20,…)` finds the species' member entry, and reads that member's own
   `uint[]` at `memberEntry+0x08..0x10`. That member `uint[]` is built by **`ID_83025`→`ID_83024`**, which
   pulls form-ids out of two **GLOBAL singleton catalog objects `ID_909810`/`ID_909812`** (selected by
   `*(species+0x2e)=='K'`), reading `*(catalogEntry+0x28)` (a form-id) for each entry whose membership
   predicate `ID_71422` passes. The catalogs are **global, not per-planet** — which is exactly why the
   flora set is universal and the fauna set is 3-shared + 1 per-species. The member `uint[]` is therefore
   **runtime-materialized from global category catalogs filtered per species**, NOT parsed from the
   planet's PNDT/PPBD record. The PPBD biome data only supplies the species LIST (FLOR/NPC_ ids), not
   the attribute-marker ids.

2. **What the markers ARE.** They are **engine attribute-CATEGORY marker forms** — fixed ESM static forms
   exposed at runtime as named global singletons. `ID_83057` (the info-panel category formatter) switches
   the marker pointer against a hard list of these singletons and calls `ID_47496(marker)` to get each
   category's display name:
   `ID_909826, ID_909814, ID_909822, ID_909828, ID_909816, ID_909818, ID_909820` (+ `ID_909832/834/836`
   as their per-category sub-markers). i.e. `0x0023E90D`=Resource-category marker, `0x002634BE`=Biomes,
   `0x0023E90C`=Genetics, `0x00171867`=flora-Reproduction, `0x002634C2`=fauna-Reproduction, etc. They are
   **category KEYWORD/marker forms** (low `0x00xxxxxx` ids), NOT the planet's species ids, NOT per-ref
   records, NOT resource records. Confirmed: none of the marker values is any planet species id; the set
   is heavily deduped/shared across species, consistent with a small fixed registry of category forms.

3. **The fauna-X.** X is the species' **primary trait / temperament / resource-affinity category form**,
   sourced from the species' own form data, NOT from the planet biome. Two engine routes converge on it:
   - `ID_83053` `'.'`-branch (fauna): walks the matched member's paired-form table `param_2[10]` (triples),
     takes the paired marker `piVar7[1]`, resolves it (`ID_47401`), and — when its form-type is `'('`
     (a component/affinity list) — iterates `form[0x47]` / `+0x238` keeping entries of form-type `0x9F`
     (`-0x61`). That kept category form is X.
   - `ID_98120` mirrors this: for a fauna base (`*(form+0x2e)=='.'`) it pulls the actor's affinity list
     (`+0x260` → `+0x120[i]` components of type `'('`, entries of type `0x9F`) to decide the species'
     primary category. So **X = the fauna species' authored primary-resource/temperament category form**
     carried on the NPC_/actor base form (its component/affinity list), resolvable from the species form
     without the planet.

4. **Ref-free verdict.** The marker **VALUES are static and ref-free** (they're fixed category-form ids,
   and the mod already hard-codes the validated sets and builds the array safely via `ID_35755`). What is
   **materialization-bound is only the engine's automatic FILL** of `slot+0x08` (`ID_52158` reads the live
   `ID_937609+0x160` biome). The mod does NOT need the live biome: it can push the known category set
   directly (already proven in-game with `TestBuildArray`/`PushSpeciesAttr`). The ONE genuinely
   species-dependent value, fauna-X, is derivable from the **species NPC_ form's own affinity/component
   list** (route in answer 3) — but that data is not in the mod's current PPBD parse, so the practical
   recipe is a **static species→X table** (small; ~6 distinct X values observed) keyed by the fauna form,
   with a safe on-visit fallback (read the member `uint[]` when present and cache). See §5.

---

## 1. The fill path, traced literally

### 1.1 `ID_52158` biome pass (the engine writer) — reads the LIVE biome
`re/ghidra/output/real-scan-chain.txt:251-355` + this pass:
```
lVar16 = *(ID_937609 + 0x160);            // CURRENT biome (materialized, on-planet only)
ID_56887(lVar16+0x20, local_c8, &species) // FNV lookup of species in biome member table (0x28 stride)
// member entry = *uVar28 + memberIdx*0x28 ; its own uint[] is [member+0x08 .. member+0x10]
if (memberArrayLen < 4) ID_83025(lVar16, speciesForm, species); // (re)materialize the member uint[]
bVar14 = (member+0x10 - member+0x08) >> 2;                      // marker count
... copies member uint[] into scratch local_b0, dedups, then ID_35755-pushes each into slot+0x08 ...
```
The marker ids therefore originate in the **member entry's `uint[]`**, which `ID_83025`/`ID_83024` fill.

### 1.2 `ID_83025` → `ID_83024` — fills the member uint[] from GLOBAL catalogs
`attr-marker-source-raw.txt:118-145`:
```
ID_83025(biome, speciesForm, species):
  ID_56887(biome+0x20, &member, &species)           // find/realloc the member entry
  catalog = (*(speciesForm+0x2e)=='K') ? ID_909810 : ID_909812   // KYWD species vs other
  ID_83024(catalog, speciesForm, member.uintArray)  // fill member uint[]
```
`ID_83024` (`attr-marker-source-raw.txt:15-99`) loops the catalog's entries (`catalog+0x38` count,
`catalog+0x40` array, `catalog+0x50/0x60` map, `catalog+0x88` base index), and for each entry that
passes the membership predicate `cVar3 = ID_71422(entryPredicate, &speciesKey)` it appends
`*(int*)(entryForm+0x28)` — **the category marker form's id** — into the member `uint[]`.
So: **catalog (global) × species (filter) → the per-species marker set.** The catalog is global; the
species only selects WHICH category markers apply, which is why the sets are universal-with-one-variable.

### 1.3 `ID_909810` / `ID_909812` are runtime singletons (null in the static exe)
`attr-marker-source-raw.txt:3133-3177`: both are zero-initialised data globals with a single READ xref
(from `ID_83025`); they are populated during engine/ScannableUtils init via computed stores (no static
WRITE xref). They hold the **attribute-category registry** (the category marker forms + their per-species
membership predicates). The exe ships them empty → the marker ids are NOT byte-constants in the binary,
but they ARE fixed ESM form-ids (validated dump values), hence constant across runs.

---

## 2. What the markers ARE — category marker forms (definitive)

`ID_83057` (`attr-marker-source-raw2.txt:33-125`) is the **survey info-panel category formatter**. It is a
straight switch of the marker pointer against the named category singletons, each rendered by
`ID_47496(marker)` (form-name getter):
```
if (marker == ID_909826) ID_83055(...)            // a list category (Resource members)
else if (marker == ID_909814) "%s %d" with a numeric (a scalar attribute)
else if (marker == ID_909822) ID_83056(...)        // fauna temperament/affinity category (calls ID_83052)
else if (marker == ID_909828) ... walks species component[0x108]+0x78 triples (Genetics/Biomes members)
else if (marker == ID_909816) + ID_909832 sub-marker
else if (marker == ID_909818) + ID_909834 sub-marker
else if (marker == ID_909820) + ID_909836 sub-marker
else                          ID_47496(marker) name only
```
So the `slot+0x08` ids are exactly these **category marker forms** (Resource / Biomes / Genetics /
Reproduction / Temperament / …). They map 1:1 to the four info-panel rows. The flora set
`[Resource, Biomes, Genetics, Reproduction]` = `[0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867]`; the
fauna set swaps in the fauna-Reproduction marker `0x002634C2` and prepends the species' primary-category
X. These are **static category forms** (the engine resolves them once at init), not per-planet, not
per-ref. (RTTI corroboration: the only survey RTTI in the dump is
`ScannableUtils::InitialScanStatus` on the slot-create command `ID_83005`.)

`ID_47400(formId)` (`attr-marker-source-raw.txt:2960-2978`) = read `*(form+0x2e)` = the TESForm
**formType byte**. In `ID_52158`, `cVar=ID_47400(species)`: `'2'`(0x32)→flora (`ID_69506` threshold),
`'.'`(0x2E)→fauna (`ID_69507` threshold). `'K'`(0x4B)=KYWD selects catalog `ID_909810`. `ID_69506/69507`
(`attr-marker-source-raw.txt:2636-2697`) return the **required marker COUNT** for a full scan per kingdom
(GMST-derived: flora 4, fauna 4) — i.e. how many of these category markers must be present to count
"fully catalogued".

---

## 3. The fauna-X source (species-specific marker)

X is the fauna species' **primary trait / resource-affinity / temperament category form**, carried on the
NPC_/actor base form, not on the planet. Evidence:

- `ID_83053` (`attr-marker-source-raw.txt`/`raw2`) `'.'`-branch: matches the species in the member's
  paired-form table `param_2[10]` (triples `{speciesId, marker, n}`), takes `piVar7[1]` (the paired
  marker), `ID_47401`-resolves it; if its formType is `'('` (a component/affinity LIST), iterates the
  list (`form[0x47]` then `+0x238`) keeping entries of formType `0x9F` (`-0x61`) — those are the kept
  category forms → X.
- `ID_98120` (`attr-marker-source-raw2.txt:919-1031`): for a fauna base (`*(form+0x2e)=='.'`) it reads the
  actor's affinity component (`form+0x260` → `+0x120[i]` of type `'('`, entries of type `0x9F`) to decide
  the primary category. Same shape, off the **species form**, no planet needed.
- `ID_83056` (`attr-marker-source-raw2.txt:337+`) is the fauna-temperament formatter for `ID_909822`,
  calling `ID_83052`→`ID_83059`→`ID_83053` — the same affinity walk — to print the per-species value.

So fauna-X is **authored on the NPC_ species** (its affinity/component list), and is in principle
derivable off-planet by walking that list. The observed small X set (`0x002634AE/AD`, `0x00280178`,
`0x001699B2`, `0x00280172`) are themselves category marker forms (same `0x00xxxxxx` registry), selected
per species by its affinity.

---

## 4. Why the engine path needs the planet, and the mod does not

`ID_52158` fills `slot+0x08` only because it reads `ID_937609+0x160` (the live biome) and re-derives the
member `uint[]` via the global catalogs. That is the on-planet wall. **But the contents it would write are
the static category markers** — and the mod has already proven (in-game, `TestBuildArray`/`PushSpeciesAttr`)
that pushing the known set directly into `slot+0x08` via the engine allocator (`ID_35755` grow /
`ID_35770` alloc / `ID_35757` update+free) renders **proper green + info**, ref-free, no biome, no spawn.
The materialization dependency is an artifact of the engine's auto-fill, not of the data.

The only species-dependent unknown for a fully-general ref-free build is **fauna-X per species**. It is NOT
in the mod's PPBD parse (PPBD gives the species list + keywords + flora, not the actor affinity). Options:
- **(A) Static table (recommended now).** Ship a `faunaForm → X` map. The X universe is tiny (a handful of
  category forms). Build it once by dumping each visited fauna's `slot+0x08[0]` (the leading X) — the mod
  already logs `slot+0x08` arrays in `DumpSpeciesSlots`. This is robust and ESM-independent.
- **(B) Parse the NPC_ affinity off the ESM.** Walk each fauna's actor record component/affinity list
  (the `0x9F`-typed category entries per §3) in `EsmReader`. Higher-fidelity but needs NPC_ record parsing
  the mod doesn't do yet (PNDT/PPBD only).
- **(C) On-visit cache fallback.** When the player is on a planet with the biome materialized, read each
  member's `uint[]` (the engine already filled it) and cache `species→markers`; use the cache for remote
  greens. Belt-and-braces behind the guarded native latch.

---

## 5. REF-FREE RECIPE for the galaxy command (build the full set, no visiting)

For each species in the planet's PPBD list, push into its `slot+0x08` (engine-allocated, via the proven
`Engine::PushSpeciesAttr`/`ID_35755` path) the marker set by kingdom:

```
formType = ID_47400(speciesForm)        // or: species came from PPBD flora vs fauna list
if FLORA ('2'):
    push [0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867]      // Resource, Biomes, Genetics, Reproduction
    // multi-biome/multi-attribute flora: also push 0x00171869 (the 5th marker) when applicable
if FAUNA ('.'):
    X = faunaXTable[speciesForm]   // static map (option A), else affinity-parse (B), else on-visit cache (C)
    push [X, 0x0023E90D, 0x002634BE, 0x002634C2]               // primary, Resource, Biomes, fauna-Reproduction
```
Notes / guardrails:
- **Engine-owned alloc is mandatory** (engine frees `slot+0x08` via `ID_35771` on teardown/rehash —
  `ID_124837`/`ID_124839`/`ID_124898`). Use `ID_35770(n*4,4)` / push via `ID_35755`. A foreign pointer =
  bad-free crash. (Already correct in the mod.)
- **Dedup** before push (the engine path dedups; `ID_52158` checks the existing array before append).
- The `(938333|planetId)` entry + species slot must exist first (the data sweep / `ID_124898` create) so
  there is a `slot+0x08` to grow. Already done by the galaxy data sweep.
- **Render refresh:** a remote build won't repaint until the player visits/refreshes; on-visit the outline
  re-queries and shows green. (Consistent with the live-repaint note in `re_green_outline.md`.)
- **Fauna-X is the only derivation gap.** Flora is fully solved and universal. For fauna, ship option (A)
  now (static table from `DumpSpeciesSlots` over a fauna sample), optionally upgrade to (B) ESM affinity
  parse later. Either makes the full fauna set ref-free.

---

## 6. One-line answers to the brief

- **Biome-member source:** the member `uint[]` is built by `ID_83025`→`ID_83024` from **global singleton
  category catalogs `ID_909810`(KYWD species)/`ID_909812`(other)**, filtered per species by predicate
  `ID_71422`, appending `*(categoryForm+0x28)`. It is runtime-materialized from a GLOBAL registry, NOT
  parsed from the planet's PPBD/.biom. The biome (`ID_937609+0x160`) only triggers/holds the per-species
  member list; the marker VALUES come from the global catalogs.
- **What the markers are:** fixed **attribute-CATEGORY marker forms** (Resource/Biomes/Genetics/
  Reproduction/Temperament…), low `0x00xxxxxx` static ESM forms, exposed as named global singletons
  (`ID_909814/816/818/820/822/826/828/832/834/836`) and consumed by category in the info-panel formatter
  `ID_83057`→`ID_47496`. Not species ids, not resource records, not per-ref.
- **Fauna-X:** the species' **primary trait/temperament/resource-affinity category form**, authored on the
  NPC_/actor base (its `0x9F`-typed affinity/component list; walked by `ID_83053`/`ID_98120`/`ID_83056`),
  selected per species — derivable from the species form, not the planet.
- **Ref-free verdict:** YES. Marker values are static and the mod already builds `slot+0x08` ref-free
  (flora fully solved, universal). The lone species-dependent piece (fauna-X) is derivable from the NPC_
  affinity, or — practically — a tiny static `fauna→X` table (option A) / on-visit cache (option C). The
  engine's auto-fill needs the live biome; the mod's direct build does not.
