# Astrophysics / off-planet TRAIT-KNOWN — synthesis (2026-06-24)

Synthesises the `astro-*-2026-06-24.txt` Ghidra dumps + `astro_*.py` ESM probes against
`src/Main.cpp`. **It overturns the working hypothesis in this session's prompt and in
`re_trait_scan_targets.md`.** Read the TL;DR first.

---

## ★ TL;DR — the hypothesis was a misread; the mod is already correct

The prompt hypothesised:
> `937887` = the planet TRAIT-KNOWN store; `ID_52155` reads `937887` via `ID_52205` as a GATE,
> and only writes `938333` if that gate is satisfied; the off-planet (Astrophysics) path sets
> `937887` first; the mod never sets `937887`, so the panel rejects.

**Every load-bearing claim there is false.** Decompiled facts:

1. **`937887` is `CTProxyFormPtr` ("ProxyFormPtr")** and **`938333` is `PlayerKnowledge`** —
   proven by NAME in the BSGalaxy::ModuleState factory registration `ID_124590`
   (`durable-readers-2026-06-24.txt:755-757`, `planet-context-setters.txt`):
   ```
   ID_126671(uVar7, param_1 + 0x2d, &ID_937887, "ProxyFormPtr",    0);   // 937887
   ID_126671(uVar7, param_1 + 0x2f, &ID_938333, "PlayerKnowledge", 0);   // 938333
   ```
   They are **distinct sibling discriminators at distinct factory slots (0x2d vs 0x2f)**.
   The dtor `ID_124591` unregisters both. Neither gates the other.

2. **`937887` is NOT a trait-known store.** It is a per-planet pointer to the planet's *proxy
   form*. `ID_124903` (the `937887` reader) returns the proxy-form pointer at component `+0x20`
   (`canonical-source.txt`, `astro-orbital-trait:576-594`). `ID_52205` reads it only as ONE of
   three **CTDA condition sources** to validate the trait belongs to the planet (see §2).

3. **`938333` (PlayerKnowledge) IS the trait-known store** — AND the species-scan store. The
   trait-known set is the planet's `938333` member-array; `ID_52156` writes it
   (`trait-known-writer.txt:240` — `key=(938333<<48)|(planetId<<16)`).

4. **The mod's existing `MarkTraitKnown` (`ID_52155`) is already the complete, correct,
   ref-free, off-planet, all-planets path.** No `937887` pre-write is needed or possible
   (`937887` is engine-authored planet definition data, not a writable knowledge slot).
   `src/Main.cpp:339 MarkTraitKnown` → `ID_52155(planetId, keyword, true)` is exactly the
   Astrophysics/off-planet trait path. The Papyrus sweep already drives it galaxy-wide:
   `CompletePlanetSurveyQuest.psc:400  MarkTraits(p, p.GetKeywordTypeList(44))`.

**Action for src/Main.cpp: NONE required for trait-known.** The "100% but panel rejects"
symptom is NOT a trait-known write problem. See §6 for what it actually is and where the real
open item lives (the on-planet 939118 scan-target byte/count, already tracked in
`re_trait_scan_targets.md`).

---

## 1. Component map (decompiled, definitive)

| Discriminator | Name (from `ID_124590`) | Key | Slot/byte | Durable? | Written by | Read by |
|---|---|---|---|---|---|---|
| **937887** | `CTProxyFormPtr` | `(937887<<48)\|(planetId<<16)` | proxy-form ptr @ comp `+0x20` | YES (saved) | engine planet authoring (NOT a knowledge write) | `ID_124903` → used as CTDA source by `ID_52205` |
| **938333** | `PlayerKnowledge` | `(938333<<48)\|(planetId<<16)` | per-planet subobj; trait set + species slots | YES (saved) | `ID_52156` (traits), `ID_52158`/`ID_124898` (species) | `ID_52154` (IsTraitKnown), `ID_52159` (green outline), `ID_97851` (survey %) |
| **939118** | `ScannableComponent` | `(939118<<48)\|(refFormID<<16)` | `+0x28` scanned byte | **NO** (stub serializer, reset on materialize) | `ID_83008`/`ID_83043` | `ID_83007` (outline), `ID_90522` (N/M count) |

939118 is registered SEPARATELY (BGSScannable, `ID_17021`), is NOT in the galaxy saved set
(`ID_124591` never unregisters it), and is zeroed on every materialization — hence the
on-planet scan-target outline/count is genuinely not durable / not all-planets. That is the
ONLY part that is materialization-bound; trait-KNOWN is fully ref-free.

---

## 2. `ID_52205` is a CTDA condition EVALUATOR, not a writer (the "gate" decoded)

`void ID_52205(int* outResult, db** param2, int planetId, {char* outByte, u32 kwFormId}* param4)`
(`trait-known-writer.txt:1-201`). It sets `*outResult` to 0/1 and writes `*(*param4)` (the
caller's result byte). Three sequential condition blocks, each only runs while `*outResult==1`:

- **Block 1 (L48-92):** `ID_58987(planetId)` → keys `(938336<<48)|(planetId<<16)` = the planet's
  **AtmosphereModifier** form (`hinge-58987.txt:11`, gated `*(form+0x2e)==0xAD`), then
  `ID_52211` evaluates a CTDA against it.
- **Block 2 (L94-142):** `ID_124903(param2)` = **the `937887` ProxyFormPtr lookup** (1-arg form,
  returns proxy form @+0x20). If the proxy exists and `*(proxy+0x2e)==0xBA`, `ID_43026`+`ID_52211`
  evaluate another CTDA against it.
- **Block 3 (L143-162):** `ID_51710(planetId)` → planet object → `+0xc0` **trait list**; compares
  each entry's `+0x28` (keyword formID) against `param4[1]` (the target keyword formID), writing
  `*(*param4)='\x01'` when the planet's trait list CONTAINS the keyword (L155).

So `937887` is consumed purely as a **read-only CTDA reference** to validate the trait. The
"gate" that decides whether `938333` gets written is **"does this planet genuinely have this
trait keyword"** (Block 3), NOT "was `937887` written first." Nothing in `ID_52205` writes
`937887`; nothing requires a prior `937887` knowledge write.

---

## 3. `ID_52155` flow (the off-planet trait-known writer the mod already calls)

`void ID_52155(u32 planetId, BGSKeyword* kw, bool known)` (`astro-orbital-trait:1-153`,
`src/Main.cpp:51 SetTraitKnownNative {REL::ID(52155)}`):

1. `kw` must be in the registered PlanetTrait keyword set `ID_896786[ID_896785]` (L43-45) —
   else no-op. (This is "is it a PlanetTrait keyword".)
2. Build `param4 = {&resultByte(='\0'), kwFormId=*(kw+0x28), 0}`; `resultByte` pre-zeroed (L60).
3. `ID_52205(&out, &db, planetId, &param4)` (L80) — evaluate "planet has this trait" → sets
   `resultByte`.
4. **GATE (L97):** `if (resultByte != '\0')` → the trait IS on the planet:
   - `ID_52156(&{planetId, kwFormId, known}, &db)` (L116) — **WRITE the `938333` PlayerKnowledge
     trait set** (add/remove kwFormId per `known`).
   - fire `StarMap::PlanetTraitKnownEvent` (`ID_52210`/`ID_123824`, L140-145).
5. `ID_97853(&{planetId,...})` (L147) — **survey recompute** (unconditional): re-runs
   `ID_1016657` aggregator, fires `PlayerPlanetSurveyProgressEvent` / `…CompleteEvent`
   (`trait-known-writer.txt:420-476`).

**`ID_52156` write target (`trait-known-writer.txt:205-392`):**
`key=(938333<<48)|(planetId<<16)`; if the planet's `938333` entry is absent → build array,
insert/remove `kwFormId`, `ID_52204` DB-insert (the `CreateAndDeleteCommand<…PlayerKnowledge>`);
if present → in-place `ID_42204`. This is a normal, saved `938333` write.

**Consequence:** `MarkTraitKnown(planetId, kw)` works for ANY planet, ref-free, off-planet,
**provided the planet's `938333`/`937887`/`938336` components are materialized in the knowledge
DB** so `ID_52205`'s condition blocks resolve. The Papyrus pass guarantees this by resolving the
planet via `Game.GetForm(fid) as Planet` + `GetKeywordTypeList(44)` (engine-materialized PNDT),
which is why the mod's trait pass already succeeds (it sweeps the whole galaxy).

---

## 4. Reader sufficiency — what consumes `938333` for trait display & survey %

- **IsTraitKnown `ID_52154`** (`astro-traitdisplay:1297-1384`, `src/Main.cpp` PlanetData glue
  `ID_114890`): `key=(938333<<48)|(planetId<<16)` (L1338), walks the trait member array,
  returns whether `kwFormId` is present. **Reads `938333`** — the panel "TRAITS N/N" and
  `Planet.IsPlanetKnown`/trait queries hit this. NOT `937887`, NOT 939118 (the CLOC scan-target).

- **Green outline `ID_52159`** (`q2-membership-key.txt:1-71`): resolves planetId from the player
  ref via `ID_52188`, `key=(938333<<48)|(planetId<<16)` (L40), returns slot `+0x21` for a species
  via `ID_124901` FNV hash. This is the SPECIES-biome green; the scan-target ACTI outline is the
  SEPARATE `ID_83007`/939118 path (NOT 938333) — that's the on-planet-only piece.

- **Survey % `ID_97851`** (`astro-starmap-handler:2369-2476`): `key=(938333<<48)|(planetId<<16)`
  (L2386); reads attribute bits at subobj `+0x20` and walks the aggregator trait arrays
  (`+0x230/+0x238`, `+0x218/+0x220` from `ID_1016657`), counting per-keyword `+0x21` bytes via
  `ID_124901` (L2403-2473). **Traits count toward survey % via `938333`.** So a trait marked by
  `ID_52155` both shows in the panel AND advances the survey number — off-planet, no CLOC ref.

The on-planet in-world "Unexplored Ecological Feature"/`PlanetTraitScanTarget` ACTI object is the
939118/CLOC path (`ID_83007`/`ID_90522`), which is Location-bound and NOT what the panel
"TRAITS N/N" or survey % read. It is correctly out of scope for off-planet completion.

---

## 5. The off-planet, ref-free, all-planets recipe (already implemented; documented for clarity)

Per planet, for each trait keyword the planet has (PNDT `KWDA` type-44, i.e.
`Planet.GetKeywordTypeList(44)`):

```
SetTraitKnownNative(planetId, keyword /*BGSKeyword* */, /*known=*/true);   // ID_52155
```

That single call:
- validates the trait via `ID_52205` (937887/938336/planet-trait-list CTDA),
- writes the durable `938333` PlayerKnowledge trait set via `ID_52156`/`ID_52204`,
- fires `PlanetTraitKnownEvent`,
- recomputes survey % via `ID_97853` (which fires the Survey-Complete slate at 100%).

Key facts for a from-scratch reimplementation:
- `planetId` = `*(u32*)(planetForm + 0x54)` (the FormID identity; `ReadPlanetId`,
  `src/Main.cpp:151 kPlanetIdOffset`). `ID_114891` passes exactly `*(param3+0x54)`.
- REL IDs: writer `ID_52155`; condition `ID_52205`; `938333` write `ID_52156`; DB-insert
  `ID_52204`; survey recompute `ID_97853`; reader `ID_52154`. DB = `*(ID_126578()+0x8B0)`,
  container `db+0x268`.
- The planet must be **materialized** (its `938333`/`937887`/`938336` components present in
  `db+0x268`). Resolving the PNDT via the form registry + `GetKeywordTypeList(44)` (as the mod's
  Papyrus pass does) satisfies this; a raw `ID_52155` on a never-touched planetId whose
  AtmosphereModifier/proxy/PlayerKnowledge entries are absent would have `ID_52205` Block 1/2
  short-circuit and the write would not fire. (The mod already orders discover→finalize→
  MarkTraits to guarantee materialization.)

The mod does NOT need to add anything. `MarkTraitKnown`/`MarkTraitKnownForPlanet` ARE this recipe.

---

## 6. What the "100% but panel rejects" symptom actually is (NOT trait-known)

Trait-known and survey % go through `938333` and are already correct off-planet (§4). The
remaining open symptom in `re_trait_scan_targets.md` ("N/M count reads 0/2") is the **on-planet,
in-world `PlanetTraitScanTarget` ACTI** — the 939118/CLOC store (`ID_83007` outline,
`ID_90522` N/M count, byte `+0x28`), which is:
- transient (stub serializer, zeroed on materialize),
- per-loaded-ref (no id-keyed durable record colors it),
- NOT what the panel "TRAITS" list or survey % read.

That is a separate, materialization-bound surface-object problem (correctly being chased via
Frida in `re/frida/`), and it does NOT block off-planet trait completion. There is no
`937887`-pre-write fix to apply, because `937887` is not a knowledge-write target.

---

## 7. ESM probe corroboration (`astro_*.py` / `re/esm`)

The ESM JSONs (`avif_handscanner.json`, `handscanner_kywds.json`, `target_dumps.json`) catalog
the HandScanner trait/genetics/reproduction KYWDs and the `PlanetTraitScanTarget` ACTIs. They
confirm the static mapping the mod hardcodes (`kTraitScanTargets`, `GetTraitScanTargetActi`,
27 traits, 4 with a 2nd ACTI) and that trait keywords are PNDT `KWDA` type-44 forms — i.e. the
planet's trait set is fully ESM-derivable, with no live instance required for the trait-KNOWN
write. They add nothing that contradicts §1-§5; they support that the off-planet trait path is
data-complete.

---

## Source index (this session's reads)

- `astro-orbital-trait-2026-06-24.txt` — `ID_52155` (1), `ID_52156` (157), `ID_52205` (348),
  `ID_124903` (576), `ID_124898` (757), `ID_52188` (832), `ID_124901` (931).
- `astro-traitknown-condition-2026-06-24.txt` — `ID_65307 BodyIsPlanetTraitKnown` (4),
  `ID_124590` galaxy registration (39).
- `astro-starmap-handler-2026-06-24.txt` — green handlers `ID_90491` (1540)/`ID_90548` (1683),
  survey recompute `ID_97851` (2369, reads 938333 @2386)/`ID_97850` (2480).
- `astro-traitdisplay-2026-06-24.txt` — `ID_124905` generic 937887/938333 create-command (1029),
  `ID_52154 IsTraitKnown` reads 938333 (1297/1338).
- `astro-eventfirers-2026-06-24.txt` — `ID_114891`/`ID_114889` PlanetData SetTraitKnown glue (1).
- `durable-readers-2026-06-24.txt` — `ID_124590`/`ID_124591` register/unregister 937887+938333 (492/763).
- `trait-known-writer.txt` — `ID_52205` (1), `ID_52156` (205), `ID_52210` event (396),
  `ID_97853` survey recompute (420).
- `q2-membership-key.txt` — `ID_52159` green-outline reader (reads 938333) (1).
- `hinge-58987.txt` — `ID_58987` AtmosphereModifier resolver (1).
- `canonical-source.txt` — `ID_124903` (937887 reader) + `ID_52188` planet-id resolver.
- `src/Main.cpp` — `MarkTraitKnown` (339), `SetTraitKnownNative ID_52155` (51),
  `MarkTraitKnownForPlanet` (1328), galaxy sweep (1105-1188).
- `CompletePlanetSurveyQuest.psc` — `MarkTraits`/`GetKeywordTypeList(44)` (226/347/400/574).
