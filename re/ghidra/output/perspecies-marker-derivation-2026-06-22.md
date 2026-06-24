# Per-species attribute-marker derivation — ref-free verdict (2026-06-22)

**Question.** Given a FLOR/NPC_ species form, compute that species' CORRECT full `slot+0x08`
`uint[]` marker set OFF-PLANET (ref-free) — the same array the engine's `ID_52158` biome pass
writes. In-game proof established the set is **per-species** (not per-kingdom): a hardcoded flora set
greened some flora but left others BLUE because their genetics/reproduction/resource VALUES differ.

**VERDICT: Path (A) is correct and ref-free.** Call the engine's per-species builder `ID_83024`
DIRECTLY with `(globalCatalog, speciesForm, &yourArray)`. It reads ONLY the species form + the
global catalog singletons; it touches the live biome NOWHERE. The biome dependency lives entirely in
the *wrapper* `ID_83025` (which uses the biome only as the storage destination for the member entry)
— and that wrapper is bypassable. (B) is the manual replica and is unnecessary given (A); (C) remains
a valid belt-and-braces cache. Full evidence below.

Decompiles/asm produced this pass (1.16.236 Ghidra project; offsets confirmed identical on 1.16.244
per `offset-skew-236-vs-244.md`):
- `re/ghidra/output/perspecies-missing-decomps.txt` — `ID_71429`, `ID_65772/3/4/6`, `ID_47401`, `ID_36524`
- `re/ghidra/output/perspecies-83024-asm.txt` — `ID_83024` assembly (context-struct construction)
- `re/ghidra/output/perspecies-globals-probe.txt` — `ID_44922`, `ID_69739`, `ID_56887`
- prior: `attr-marker-source-raw.txt` (`ID_83024`/`ID_83025` C), `attr-marker-source-raw2.txt`
  (`ID_71422` C), `real-scan-chain.txt` (the `ID_52158` biome pass)

---

## (A) Call `ID_83024` directly — THE ref-free path

### A.1 Exact signature

```c
// REL::ID(83024) @ 0x141309590 (236)
//   param_1 = catalog  : the GLOBAL category-catalog singleton (ID_909810 for KYWD species,
//                        else ID_909812). Always loaded post-init.
//   param_2 = species  : the FLOR / NPC_ base TESForm* (the species form).
//   param_3 = outArray : pointer to a BSTArray<uint32_t> HEADER {begin@+0, end@+8, capEnd@+0x10}.
//                        ID_83024 appends u32 marker ids onto it via the ENGINE allocator (ID_35755).
void ID_83024(void* catalog, RE::TESForm* species, BSTArrayU32Header* outArray);
```

`ID_83024` does NOT return the array — it APPENDS into `param_3` (so seed it empty and read it back).

### A.2 What it reads — literally (proves no biome / no world)

From `attr-marker-source-raw.txt:15-99` (C) + `perspecies-83024-asm.txt` (asm):

```
for (i = 0; i < *(catalog + 0x38); i++) {           // catalog entry count @ +0x38
    base   = *(catalog + 0x88);                       // FNV map base index
    idx    = ID_36524(catalog + 0x50, i - *base);     // hashmap probe -> condition object
    cond   = *(catalog+0x60 + idx*0x18 + 8);          // catalog entry's CONDITION object  (R12)
    form   = *(catalog+0x40 + (i-*base)*8) OR ID_47401(*(catalog[i]));  // entry's category FORM  (RBP)
    if (cond != 0) {
        // ---- context struct built ENTIRELY from the species form ----
        ctx[0x00] = species;        //  asm: MOV [RSP+0x20], <stored param_2>
        ctx[0x08..0x88] = 0;        //  asm: VPXOR-zeroed (0x28,0x30,0x40,0x50,0x58,0x60,0x70,0x80,0x88)
        if (ID_71422(cond, &ctx) == 0) continue;       // membership predicate — species-only
    }
    marker = *(int*)(form + 0x28);  // the CATEGORY MARKER form-id
    append marker -> outArray (ID_35755 grow when full, else in-place)
}
```

**The only inputs are `catalog` (global) and `species` (the form).** No `ID_937609+0x160`, no biome,
no player, no planet anywhere in the body (verified in both the C and the raw asm). The asm at
`1413095d`–`1413096b2` shows the context struct = species-form-at-offset-0, everything else zeroed,
passed straight to `ID_71422`.

### A.3 The membership predicate `ID_71422` reads species-only (proof it's ref-free)

`ID_71422(cond, &ctx)` (`attr-marker-source-raw2.txt:3049`) is the generic Bethesda
**condition-list evaluator** (TESCondition::IsTrue; ~250 callers). It:

1. **Saves+nulls the world/quest TLS context** via `ID_65772/3/4/6` — these are TLS getters/setters
   (`perspecies-missing-decomps.txt:705-801`): each does `old = TLS[off]; TLS[off] = arg; return old`
   for TLS offsets `0x1f0 / 0x258 / 0x250 / 0x218`. `ID_83024` passes `ctx+0x10/0x18/0x20/0x28`
   = **all zero** → the quest/event/target run-on context is explicitly cleared for the eval, then
   restored after. So the condition is evaluated with NO live quest/scene/reference context.
2. Walks the condition linked list (`cond = *(cond+8)`, AND/OR via `cond+0x38 & 1`), calling
   `ID_71429(condItem, &ctx)` per item, AND/OR-combining.

`ID_71429` (`perspecies-missing-decomps.txt:1-701`) switches on `*(condItem+0x39)` (the authored
**run-on / target type**) to choose the subject, then dispatches the authored comparison function
`(&ID_896671)[condFunc*0xb]` against the subject and the condition's authored args
`ctx[2..0xd]` (= ctx+0x10..+0x68, **all zero** here). The subject-selection cases that matter for
attribute-category membership are species-form-local:
- **case 1** "subject" = `*ctx` = the species form.
- **case 3 / case 10** ('K'/KYWD): subject derived from the species form when `*(form+0x2e)=='K'`
  (matches the `ID_909810` KYWD-catalog selector) — i.e. the species' own keyword form.
- **case 4 / case 6**: resolve another form (`ID_47401`) and run a getter (`ID_44922`/`ID_69739`)
  on the **species form's component/keyword container** `species[0x19]` — species-local
  (`perspecies-globals-probe.txt`: `ID_44922` walks `param_1`'s linked-ref/component list;
  `ID_69739` is a small `(form, typeByte)`→entry table scan).

**Caveat (the one honest risk for (A)).** `ID_71429` cases **8 / 9 / 14** reach live globals
(`ID_922868` = the data-handler/TES singleton at +0xf90/+0x102957; `ID_937788` = a process/player
list). Those cases only fire if a catalog entry's authored condition uses a *world/global* run-on
type. For attribute-CATEGORY membership the authored conditions are **species-property tests**
(HasKeyword / GetType / affinity on the subject), not world-state — which is exactly why the
on-planet `ID_52158`→`ID_83025`→`ID_83024` produces a *deterministic per-species* set (the same set
every visit, independent of player/time/biome instance). Since `ID_83024` is the identical call the
engine already makes, calling it directly reproduces that determinism. If a future catalog entry did
use a world run-on with nulled context, it would simply evaluate false/neutral (context is zeroed),
never crash. **Net: ref-free and safe.**

### A.4 Why the engine's auto-fill *looks* biome-bound (and isn't, for the data)

`ID_83025` (`attr-marker-source-raw.txt:118-145`) is the wrapper the biome pass calls:

```c
void ID_83025(void* biome, TESForm* species, uint32_t speciesKey) {
    ID_56887(biome+0x20, &member, &speciesKey);   // find/alloc the species' member entry IN the biome
    catalog = (*(species+0x2e)=='K') ? ID_909810 : ID_909812;
    ID_83024(catalog, species, member.uintArray); // <-- the real work; biome only supplied the DEST
}
```

The biome is used ONLY by `ID_56887` to locate the *destination* member entry (an FNV-1a hashmap,
0x28 stride, 4-byte key — `perspecies-globals-probe.txt:224`). The marker COMPUTATION (`ID_83024`)
never sees the biome. So the mod skips `ID_83025` entirely and calls `ID_83024` with its OWN array.

### A.5 Paste-ready C++ recipe (drop into the existing engine layer in `src/Main.cpp`)

The mod already engine-owns `slot+0x08` via `PushSpeciesAttr`/`BSTArrayU32Grow {REL::ID(35755)}`.
Add the catalog singletons + the `ID_83024` binding, then either (i) fill the slot array in place, or
(ii) fill a scratch array and push each id with the existing `PushSpeciesAttr` (recommended — keeps
the single proven alloc path and lets you dedup/log).

```cpp
namespace Engine {

// Global category-catalog singletons. They are runtime REL::ID singletons: zero in the static exe,
// populated during engine/ScannableUtils init (single READ xref each, from ID_83025). Read the
// POINTER the singleton holds at call time. (raw.txt:3133-3177 = empty-in-binary confirmation.)
inline REL::Relocation<void**> AttrCatalogKYWD  {REL::ID(909810)};  // KYWD-typed species ('K')
inline REL::Relocation<void**> AttrCatalogOther {REL::ID(909812)};  // everything else (FLOR/NPC_…)

// ID_83024: append the species' correct category-marker ids onto a BSTArray<u32> header.
//   args: (catalog, speciesForm, &bstArrayHeader{begin@+0,end@+8,capEnd@+0x10})
using fn_build_markers_t = void (*)(void* catalog, RE::TESForm* species, void* outArrayHeader);
inline REL::Relocation<fn_build_markers_t> BuildSpeciesMarkers {REL::ID(83024)};

// Fill `out` (must be empty: begin==end==cap==0) with the species' engine-exact marker set.
// Returns false (degraded -> caller keeps species blue) on any fault; NEVER crashes the session.
bool BuildSpeciesMarkerSet(RE::TESForm* species, std::vector<std::uint32_t>& out)
{
    out.clear();
    if (!species)
        return false;
    try {
        // pick catalog by form-type byte at +0x2e ('K' == KYWD)
        const auto ftype = *reinterpret_cast<const unsigned char*>(
            reinterpret_cast<std::uintptr_t>(species) + 0x2e);
        void* catalog = (ftype == 'K') ? *AttrCatalogKYWD : *AttrCatalogOther;
        if (!catalog) {
            spdlog::info("BuildSpeciesMarkerSet: catalog singleton null (engine not inited?)");
            return false;
        }
        // engine-owned BSTArray<u32> header on the stack: {begin,end,capEnd} all null.
        struct { std::uint32_t* begin; std::uint32_t* end; std::uint32_t* capEnd; } hdr{nullptr,nullptr,nullptr};
        BuildSpeciesMarkers(catalog, species, &hdr);            // engine appends via ID_35755
        for (auto* p = hdr.begin; p && p != hdr.end; ++p)
            out.push_back(*p);
        // hdr.begin is engine-allocated (ID_35770). Free it the way the engine frees slot+0x08:
        // ID_35757(header) — or, simpler, just copy values out and FREE via the matching dealloc.
        // (If you instead build straight into the real slot+0x08 header, skip the copy+free entirely.)
        Engine::FreeEngineArray(&hdr);   // ID_35757 / ID_35771-style release of hdr.begin
        spdlog::info("BuildSpeciesMarkerSet: 0x{:08X} -> {} markers", species->GetFormID(), out.size());
        return !out.empty();
    } catch (...) {
        spdlog::warn("BuildSpeciesMarkerSet: 0x{:08X} faulted", species->GetFormID());
        return false;
    }
}

} // namespace Engine
```

Then in the galaxy/green build, replace the hardcoded flora set + `GetFaunaX` with:

```cpp
std::vector<std::uint32_t> markers;
if (Engine::BuildSpeciesMarkerSet(speciesForm, markers)) {
    std::sort(markers.begin(), markers.end());
    markers.erase(std::unique(markers.begin(), markers.end()), markers.end());  // dedup
    for (auto id : markers)
        Engine::PushSpeciesAttr(slotAddr, id);     // existing engine-owned push
} else {
    /* fall back to (C) on-visit cache, or leave blue */
}
```

**Cleanest variant:** pass the REAL `slot+0x08` header (`slotAddr+0x08`) straight in as `param_3`
so `ID_83024` appends directly into the engine-owned slot array — no scratch copy, no manual free,
identical to what `ID_83025` does to the member entry. Pre-dedup is then unnecessary (engine path
is what `ID_52158` later copies/dedups anyway). Guard the call with the existing native-boundary
try/catch + degraded latch.

> Open implementation detail: confirm the exact engine free routine for the scratch header
> (`ID_35757` is the update+free used inside the grow path; `ID_35771` is the teardown free). The
> direct-into-slot variant sidesteps this entirely and is the recommended form.

---

## (B) Manual per-species replica — possible but UNNECESSARY given (A)

The current mod implements a partial (B): hardcoded flora set + `GetFaunaX` (an affinity walk
replicating `ID_98120`). The in-game failure proves (B)-by-hardcode is wrong: the per-species
genetics/reproduction/resource markers are selected by the **catalog conditions**, not a fixed list.

A faithful (B) would have to re-implement `ID_83024`'s loop in C++:
- Enumerate the global catalog: `count = *(catalog+0x38)`, entry forms at `*(catalog+0x40)` (array of
  form ptrs) indexed via the `+0x50/+0x60/+0x88` FNV map, condition objects at
  `*(catalog+0x60 + idx*0x18 + 8)`, marker id = `*(entryForm+0x28)`.
- Re-implement `ID_71422`/`ID_71429` condition evaluation against the species form.

That is a full re-implementation of the engine's condition VM for zero benefit over calling
`ID_83024` directly (which IS that VM). **Do not pursue (B).** The species-form fields that the
conditions test (resource/genetics/reproduction/biome category membership) live behind the form's
keyword/component container `species[0x19]` and are consumed via `ID_44922`/`ID_69739`/`ID_37878`
getters — but you never need to read them manually because `ID_83024` does it for you.

For completeness, the catalog-entry structure (from `ID_83024` + `ID_36524`):
| offset (on `catalog`) | meaning |
|---|---|
| `+0x38` | entry count (loop bound) |
| `+0x40` | array of entry FORM pointers (`form`, whose `+0x28` is the marker id) |
| `+0x50` / `+0x60` | FNV hashmap (key table / value table, `0x18` stride) for entry lookup |
| `+0x68` | hashmap "not found" sentinel |
| `+0x88` | base index pointer (`*+0x88` = first valid index) |

`*(categoryForm + 0x28)` = the **category marker form-id** appended to the array (the `uint`).

---

## (C) On-visit cache fallback — CONFIRMED valid, keep as belt-and-braces

When the player is on a planet with the biome materialized, the engine has already filled every
member's `uint[]`. The mod can read ALL species' correct sets (not just scanned ones) and cache
`species → markers`:

```
biome  = *(ID_937609 + 0x160);                 // live biome (on-planet only)
table  = biome + 0x20;                          // member FNV-1a hashmap (ID_56887), 0x28 stride
for each speciesKey on the planet (PPBD list):
    ID_56887(table, &res, &speciesKey);         // res = {tableBase, memberIdx}
    member = *res.tableBase + memberIdx*0x28;
    if (memberIdx != notFound) {
        u32* begin = *(member + 0x08);
        u32* end   = *(member + 0x10);          // member uint[] = [begin..end)
        cache[speciesKey] = { begin .. end };   // the engine-correct per-species marker set
    }
```

`ID_56887` (`perspecies-globals-probe.txt:224`) confirms the member table is an FNV-1a hashmap
(seed `0xcbf29ce484222325`, prime `0x100000001b3`) over a 4-byte key, `0x28` stride, with the
overflow/next index at `member+0x20` and the "empty" sentinel `-1`. The member `uint[]` BSTArray is
`{begin@+0x08, end@+0x10, capEnd@+0x18}` (same layout as `slot+0x08`). Reading it is pure load — no
alloc, no engine call. Use it to seed a `species→markers` map for remote greens. This is strictly a
fallback now that (A) gives the same data ref-free; keep it behind the guarded native latch as a
self-healing cross-check (and the only source if a catalog singleton were ever unpopulated).

---

## One-line answers to the brief

- **Which path:** **(A).** `ID_83024(catalog, speciesForm, &outArray)` computes the engine-exact
  per-species set ref-free. It reads only the global catalog singleton (`ID_909810` KYWD / `ID_909812`
  other) + the species form; the biome is referenced nowhere in its body (verified in C and asm). The
  biome appears only in the wrapper `ID_83025`, which uses it solely as the storage destination —
  bypassed by calling `ID_83024` with your own array.
- **Signature:** `void ID_83024(void* catalog, TESForm* species, BSTArrayU32Header* out)` —
  `REL::ID(83024)`; catalogs `REL::ID(909810)`/`REL::ID(909812)` (singletons, deref the pointer);
  appends via `REL::ID(35755)` (already wired).
- **Does it deref the biome?** No. Only `species` (form, at context+0x00) and `catalog`. The
  condition evaluator `ID_71422` runs with the quest/target/world TLS context explicitly NULLED
  (`ID_65772/3/4/6` set ctx+0x10/0x18/0x20/0x28 = 0), so the per-entry condition tests species
  properties only. World-touching condition run-on types (cases 8/9/14 in `ID_71429`) are not used
  by category-membership conditions and, with nulled context, would no-op rather than fault.
- **If it needed the biome:** it doesn't. (Only `ID_83025`'s storage step does, and that's skipped.)
- **(B) field offsets (if ever needed):** species condition inputs are behind `species[0x19]`
  (keyword/component container), read via `ID_44922`/`ID_69739`; catalog entry layout in §B table;
  marker = `*(categoryForm+0x28)`. Not needed — (A) supersedes.
- **(C) recipe:** read each planet species' member `uint[]` at `member+0x08..0x10` from the live
  biome member hashmap `biome+0x20` (`*(ID_937609+0x160)+0x20`, FNV-1a, 0x28 stride via `ID_56887`).
  Valid as a cache/fallback; not required for the ref-free build.
