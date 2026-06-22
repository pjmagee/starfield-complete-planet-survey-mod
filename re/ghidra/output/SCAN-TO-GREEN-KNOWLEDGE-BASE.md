# Starfield Planet-Survey: The Complete Scan-to-Green (and Scan-to-100%) Pipeline

**Scope.** This is the authoritative, deduplicated reconstruction of the literal path from a player scan to (a) a *persistent green outline* and (b) *survey-% / "BIOME COMPLETE"* for the Starfield planet-survey knowledge system, synthesized from six decompile-analysis passes plus user-verified in-game ground truth. It is structured as a linear pipeline. Every claim carries an inline confidence tag and citation. It is written to be adversarially audited — disagreements are surfaced, not smoothed.

**Confidence tiers (highest to lowest):**
- `[in-game-confirmed]` — observed in real saves; **OUTRANKS any decompile claim**. When a decompile contradicts this, the decompile is wrong/incomplete.
- `[decompile-verified]` — read directly in the decompile with the cited offsets/instructions.
- `[inferred]` — reasoned from decompile-verified facts but not directly observed.
- `[assumed-unverified]` — assumption with no direct support; weakest.

**Naming.** `ID_NNNNN` are address-library REL::IDs (engine functions/forms). `db` = the BSGalaxy knowledge manager. `subobj` = the per-planet survey sub-object at `entry+0x20`. "+0x21" = the per-species scan-flag byte; "+0x20" = the per-species percent byte.

---

## 0. CONFIRMED IN-GAME GROUND TRUTH

These are user-verified from real saves. They are the spine the decompile must explain. Where the decompile disagrees, **these win**.

- **G1 — On-planet CompleteSurvey persists green.** On-planet `CompleteSurvey` (PlaceAtMe one of each species + Papyrus `SetScanned(true)` + `UpdatePlanetProgressForSpecies`=`ID_52157` + `ScanNearbyRefs`) greens the current planet AND the green **persists across a full quit-to-desktop + restart**; fresh instances render green on reload. `[in-game-confirmed]`
- **G2 — Natural scanning persists green.** Aiming the scanner at a wild creature and holding to 100% greens that species on that planet and persists across restart. `[in-game-confirmed]`
- **G3 — Off-planet ref-free pre-write failed three ways:** `[in-game-confirmed]`
  - **(a)** `+0x21` under the **RAW ESM species formId** → survey % reads 100% ("BIOME COMPLETE") **but the outline stays BLUE**.
  - **(b)** `ID_83009` on a bare PlaceAtMe'd instance → returned the instance's **own dynamic 0xFFxxxxxx formId** (no `ScannableComponent` existed).
  - **(c)** `ID_83006` on the raw ESM form off-planet → **ACCESS VIOLATION**, caught by the guard. **NOTE:** (c) crashed *before logging*, so it is **NOT proven semantically wrong**, only not-safely-callable-as-invoked.
- **G4 — Galaxy DATA sweep is ref-free and works from anywhere.** Completes %/slates/traits/resources for barren planets ref-free. `[in-game-confirmed]`
- **G5 — Console travel commands are engine stubs.** `lop`/`lopb`/`TakeOffToSpace` are `MOV AL,1; RET`. Ref-free galaxy completion is therefore *necessary*, not a design preference. `[in-game-confirmed]`
- **G6 — Instant-scan GMSTs are forced by the mod.** `iHandScannerAnimalCountBase=1`, `iHandScannerPlantsCountBase=1`, so one scan tips a species to 100% (threshold = 1). `[in-game-confirmed]`
- **G7 — Green is per-(planet, species), NOT planet-agnostic.** Scanning species X on planet A does **not** green X on a never-visited planet B. This explicitly **supersedes** the earlier "per-ref ScannableComponent / planet-agnostic / per-species galaxy-wide" verdicts. `[in-game-confirmed]`

---

## 1. TRIGGER — the player scans

There are **two distinct trigger families** that reach the knowledge DB. They write **different** structures; conflating them is the root of the historical confusion.

### 1.1 Hand-scanner / Papyrus `SetScanned` path (the one that greens)

- The Papyrus `ObjectReference.SetScanned` native is **`ID_118472`** (`@1420845e0`). It calls `ID_83007(ref)` ("is already scannable/scanned?"); **if that returns nonzero** it calls `ID_83008(ref, scannedFlag, 0xd, 0)` and returns — i.e. it enters the engine at `ID_83008` with **category byte `0xd`** (`kBiomeScanCategory`). `[decompile-verified]` (setscanned.txt:3-20)
- **Else branch (the trap).** If `ID_83007` returns 0, `ID_118472` instead resolves a canonical form via `ID_83006` and calls `ID_83005` (per-instance ScannableComponent CreateAndDeleteCommand). **This is the branch the mod's bare-PlaceAtMe'd-ref experiments hit, not the normal scan flow.** `[decompile-verified]` (setscanned.txt:9-19)
- **The mod mirrors this exactly:** `ScanRefNative = ID_83008` called with `(ref, 1, 0x0d, 0)`, byte-for-byte what `ID_118472` passes. The natural-scan UI ultimately drives the same `ID_83008` chain. `[decompile-verified]` (src/Main.cpp:51, :117, :714/:1102; setscanned.txt:11)
- **The auto-hook.** The mod installs a `write_call<5>` at the `ID_97853` call site **inside `ID_52157`**, so *every* successful biome scan (player E-key, Papyrus `SetScanned`, any path) routes through a thunk → original → `DispatchPapyrusStatic(CompleteSurveyIfEnabled)`. `[decompile-verified]` (src/Main.cpp:1226-1295)

### 1.2 Orbital / starmap scan-complete path (a different trigger)

- `ID_102651` (`@141a2ca00`, reached via `ID_102650`) is the engine's ref-free "set surveyed bit for a species id." It sets **bit 4 in `subobj+0x20`** when the `(938333)` entry exists, or creates the entry via `ID_51421`/`ID_52204` when missing, then **recurses over child species ids**. `[decompile-verified]` (scan-complete.txt:1-285)
- **This is NOT the hand-scanner accumulation.** It flips a **planet-level bit**, not a per-species `+0x21` byte. The mod uses `ID_102650(0, planetId, 1)` in the galaxy sweep to *discover/create* the knowledge entry for barren bodies. `[decompile-verified]` (src/Main.cpp:652-660, :101-108)

### 1.3 Branch discriminator: `ID_36022(base+0xc8, 0x2a)`

- Both the trigger inner (`ID_83008`) and the read-side gate (`ID_83007`) branch on a base-form property bit via **`ID_36022(*(ref+200), 0x2a)`** (`@1402c62b0`): it locks (+0x20), tests bit `0x2a` of the bitfield at `*(base+0x18)` — byte `(1 << (0x2a&7))` at offset `(0x2a>>3)` — returns 0/1, unlocks. `[decompile-verified]` (scan-deeper.txt:436-463)
- **Clear (==0) = FLORA-type branch** (per-instance handling); **Set (!=0) = ACTOR/FAUNA branch** (per-type handling). `[decompile-verified]` (scan-inner.txt:207-236)
- ⚠️ **DISAGREEMENT / OPEN:** that flora=clear / actor=set mapping is **`[inferred]`** from the model/docs; the bit's authored source on the base record was **not** verified in any decompile this round. The form-flag enum for bit `0x2a` is unestablished.

---

## 2. SCAN ACCUMULATION & THRESHOLD

### 2.1 `ID_83008` — SetScanned inner, the fork

`ID_83008` (`@141307910`) dispatches on `ID_36022(base+0xc8, 0x2a)`:

- **CLEAR (flora) branch:** builds `key28 = *(int*)(ref+0x28)` (the ref's OWN formId), builds ctx `{scannedFlag, &out_count}`, calls `ID_83038(db, &ctx, &species_id)`; **only if `out_count != 0`** calls `ID_52157(ref, out_count, scannedFlag)`. `[decompile-verified]` (scan-inner.txt:207-236; scanned-state.txt:118-149)
- **SET (actor) branch:** if `scannedByte != 0`, calls `ID_52160(ref, category, flag)` and returns. `[decompile-verified]` (scan-inner.txt:207-236; scanned-state.txt:118-149)

### 2.2 `ID_83038` — the FIND-ONLY flora writer (why bare PlaceAtMe no-ops)

- `ID_83038` (`@14130a600`) keys `(ID_939118 << 48) | (refFormID << 16)` into `db+0x268` via `ID_126806`. It writes the scanned byte at `entry+0x28` **only if the entry EXISTS** (`out[3] != 0xfe0 || out[2] != 0`), and copies the **canonical id from `entry+0x24`** out to `*param2[1]` (the value fed to `ID_52157`). `[decompile-verified]` (scan-inner.txt:479-553; real-scan-chain.txt:477-553)
- **On a miss it writes nothing, `out_count` stays 0, and `ID_52157` is never called.** This is the engine-level reason `SetScanned` no-ops on a bare PlaceAtMe'd flora ref that has no `(939118)` ScannableComponent. `[decompile-verified]` (scan-inner.txt:479-553) — explains **G3(b)**.

### 2.3 `ID_52157` — per-planet progress updater (convergence point)

`ID_52157` (`@1407b7fa0`), signature `(ref, speciesId, category=0xd, byte, notifyFlag)`:

1. Resolves planet from the **scanned ref's** location: `cVar4 = ID_52188(ref, &planetId, &biomeId)`. Bails early only if `(dynamic-id ref) && ID_46568()==0 && category!=0`. `[decompile-verified]` (real-scan-chain.txt:1-43)
2. Builds a 48-byte stack ctx `{planetId@+0x00, speciesId@+0x04, delta=1@+0x08, ref@+0x10, &outScanned@+0x18, &outPercent@+0x20, flag@+0x28}`. `[decompile-verified]` (real-scan-chain.txt:45-75; src/Main.cpp:349-362)
3. Calls **`ID_52158`** (the count/percent writer) — *the only place green is written*. `[decompile-verified]` (real-scan-chain.txt:45-75)
4. If `notifyFlag` set, calls `ID_83019(planetId, biomeId, ref, species, outScanned, outPercent)` (a second BSGalaxy event), then **always** calls `ID_97853(&ctx)` (completion check). `[decompile-verified]` (real-scan-chain.txt:98-101)

- **Key constraint:** `ID_52157` receives the **engine-resolved canonical species id** (from `ID_83038`'s `+0x24` copy-out), **not** the raw ref formId. `[decompile-verified]` (scanned-state.txt:131-143)
- The live "hold-to-100%" progress is surfaced as a float `local_cc*ID_500678` captured via `ID_1016657`; the discrete species flip happens when `+0x21` crosses threshold. Whether `+0x21` accumulates per-frame during hold or as a single delta at scan-complete is **`[inferred]`** — the scanner-UI controller that calls `ID_83008`/`ID_52157` during a held scan is in **no** assigned dump.

### 2.4 `ID_52188` — planet resolver (credits the planet you stand on)

`ID_52188` (`@1407bd600`) resolves the planet from the ref's LOCATION, in order:
1. **ExtraLocation 0x81** via `ID_37878(*(ref+200), 0x81)` → node+0x28 (planetId), node+0x30 chain (system/parent). `[decompile-verified]` (scan-deeper.txt:467-562; planet-resolver.txt:1-96)
2. Fallback `ID_56990(ref)` parentCell walk; then `ID_42691`/`ID_124846`. `[decompile-verified]`
3. Final fallback: current-planet global `*(int*)(ID_937609+0x80)`. `[decompile-verified]` (real-scan-chain.txt:390-485; planet-writer.txt:283-378)

- It returns the planet the ref is **physically on** — **NOT** a species "home" planet. This is the engine-level reason natural scans and spawn-and-scan both credit only the current planet. An `0x81` ExtraLocation on the ref **can redirect the write planet**. `[decompile-verified]`
- ⚠️ **OPEN:** which path a *freshly PlaceAtMe'd* ref (no `0x81`) takes was **not** confirmed in-decompile (both branches were read). Ground truth says it credits the planet you stand on (consistent with the final-fallback global). `[inferred]`

### 2.5 `ID_52158` — the count writer (the heart)

`ID_52158` (`@1407b81c0`) is where `+0x21` is accumulated and far more:

1. **Key & resolve.** `key = (ID_938333 << 48) | (planetId << 16)`; `ID_126806(db+0x268, &key)`. **If the entry is missing** (`local_80==0xfe0 && local_88==0`) it **RETURNS immediately, doing nothing.** `subobj = entry+0x20`. `[decompile-verified]` (real-scan-chain.txt:162-175)
2. **Pre-read was-unscanned.** Reads existing `+0x21` (bVar14) via `ID_124901(subobj+0x18, &species)` → `slot*0x30 + *(subobj+0x40) + 0x21`, stores `(bVar14==0)` into `*(param_1+6)` (the caller's outScanned). `[decompile-verified]` (real-scan-chain.txt:176-183)
3. **Write `+0x21`** via `ID_124898(subobj, species, delta)`. `[decompile-verified]`
4. **Re-read post-write `+0x21`** (bVar15), classify via `ID_47400(species)` → `'.'`→`ID_69507()` (flora threshold) / `'2'`→`ID_69506()` (fauna threshold) / else const 1; **percent = clamp(round((bVar15/bVar13)*100), 0..100)** via `ID_177246`/`ID_500678`(=100.0); store through `*(param_1+8)` (outPercent) **and** call `ID_124899(subobj, species, percent)` to write **`+0x20`**. `[decompile-verified]` (real-scan-chain.txt:185-211)
5. **Threshold event.** If `((bVar14<bVar13) && (bVar13<=bVar15) && bVar8)` → `ID_101322(ID_922868, ID_909785, 0xd, 1, ref, 0, 0)` (a PlayerKnowledgeFlagSet-style event). `bVar8` is a validity gate from `param_1[4]` (the ref/form). `[decompile-verified]` (real-scan-chain.txt:194-247)
6. **Biome propagation.** Keys off the engine's CURRENT biome at `*(ID_937609+0x160)` (NOT the write's target planet): `ID_56887((biome)+0x20, ...)` (0x28-stride FNV map); rematerializes the member list via `ID_83025` when short. `[decompile-verified]` (real-scan-chain.txt:249-309)
7. **Sibling recursion.** Builds a deduped sibling-species set; for each sibling equal to `*(uint*)(ID_909826+0x28)` it expands via `ID_83052` and **recursively calls `ID_52157(ref, sibling+0x28, 0xb, 1, flag)`**. This is how scanning ONE species can credit a whole biome group. `[decompile-verified]` (real-scan-chain.txt:311-385)

### 2.6 `ID_124898` — the literal `+0x21` mutator

`ID_124898` (`@142348ad0`), args `(subobj, species_id, delta, 0)`:
- Finds the slot via `ID_124901` (FNV-1a, 0x30 stride). **If present:** *saturating* add of delta into byte at `slot+0x21` (`if (0xff-delta) < existing → 0xff, else existing+delta`). **If absent:** **CREATES** the slot via `ID_124840`, stores species at `slot+0x00`, initializes `+0x21` from delta. `[decompile-verified]` (scan-inner.txt:557-628; real-scan-chain.txt:557-628)
- **Sets bit 3 (`|8`) in `subobj+0x00`** (dirty/known bit) on **every** call. `[decompile-verified]` (scanned-state.txt:626)

### 2.7 `ID_124899` — the `+0x20` percent byte

`ID_124899` (`@142348c20`): writes `param_3` to `*(subobj+0x40)+0x20+slot*0x30`; if slot absent, INSERTS via `ID_124840` (leaving `+0x21` at 0). Does **not** set the dirty bit, does **not** saturate. `[decompile-verified]` (scanned-state.txt:632-685; survey-compute.txt:247-300)

### 2.8 The threshold (why one scan = 100% here)

- The flip is enforced in `ID_52158`/`ID_97851` by comparing accumulated `+0x21` (bVar15) against the per-type threshold `bVar13` from `ID_69506` (fauna) / `ID_69507` (flora). With the instant-scan GMSTs (**G6**) the threshold is **1**, so a single scan tips it. `[decompile-verified]` for the comparison; `[in-game-confirmed]` for threshold=1 (real-scan-chain.txt:194-247; src/Main.cpp:753-764)
- ⚠️ **OPEN:** `ID_69506`/`ID_69507` bodies (how they derive the threshold from the GMSTs / player skill `ID_922868`) were **not** read this pass (present in hinge-58987.txt, which notes they're player-skill computations). The comparison is verified; the derivation is not.

---

## 3. CANONICAL SPECIES-ID RESOLUTION

This is the crux of the **G3(a) blue bug**. Two id domains exist: the **authored** id (ESM/PNDT/PPBD form id, low dword, stored at component `+0x20`) and the **canonical** id (engine-resolved, stored at component `+0x24`). The green renderer and the real scan must agree on which one keys the table.

### 3.1 The write-side derivation chain (`ID_83004`)

`ID_83004` (`@141307430`) creates the per-instance ScannableComponent and computes the canonical id via the **INSTANCE vtable[0x228]**:
```
plVar3 = ID_47401(form);                      // formid -> live instance/form
lVar4  = (*(plVar3+0x228))(plVar3);           // instance -> base/resolved form
if (lVar4) lVar4 = ID_83006(lVar4);           // form    -> canonical form
uStack_64 = *(u32*)(lVar4 + 0x28);            // canonical id
puVar5[4] = CONCAT44(uStack_64, uVar1);       // +0x20 = authored, +0x24 = canonical
```
where `uVar1 = *(u32*)(param_3+0x28)` (source form's own id). If the `vtable[0x228]→ID_83006` chain yields nothing, `uStack_64` was pre-seeded `= uVar1`, so **+0x24 falls back to the authored id** (identity case). `[decompile-verified]` (scan-component-lifecycle.txt:25-37, 65-66)

### 3.2 `ID_83006` — the form-level canonical resolver (the one that faults)

`ID_83006` (`@141307670`):
```
uVar2 = ID_63393();                           // default (runs unconditionally)
if (ID_64338(param_1)) {
    plVar3 = ID_44958(*(param_1+200), 0);
    if (plVar3) return (*(plVar3+0x428))(plVar3);  // base-component canonical
}
return uVar2;
```
`[decompile-verified]` (scan-inner.txt:63-84)

- **Gate `ID_64338`** (`@140ba6320`): requires `*(param_1+0x98)!=0` **AND** `*(char*)(*(param_1+0x98)+0x2e)=='2'`, then returns true if `ID_36022(base+0xc8,0x2b)` OR `ID_47225(*(param_1+0x98)+0xe8, -1)`. Returns false when `+0x98` is null or the `+0x2e` tag is not `'2'`. `[decompile-verified]` (canonical-chain-2026-06-21.txt:46-63)
- **Default `ID_63393`** (`@140b4d430`): reads `*(param_1+0x98)`; if null falls to `ID_44957(*(param_1+200))`; else walks a keyword/component list (`ID_47393` over `ID_937842`) for a `+0x2e=='B'` entry, resolving a form via `*(param_1+0x28)` through `+0x238`/`ID_63938`. Can return `*(param_1+0x98)` (the base form) itself. `[decompile-verified]` (canonical-chain-2026-06-21.txt:67-162)
- **Why G3(c) faulted:** `ID_83006` unconditionally dereferences `param_1+0x98` then `+0x2e` (in `ID_64338`) and `param_1+0x98`/`+0xC8` (in `ID_63393`) with **no null/type pre-validation**. Handed a form that is not the expected scannable-base shape, it faults. `[inferred]` (scan-inner.txt:72-73; canonical-chain-2026-06-21.txt:48-54)
- **The mod's bug-equivalent:** `CanonicalFormId(form)` calls `ID_83006(form)` directly and reads `+0x28`, **skipping the `vtable[0x228]` step** the engine performs in `ID_83004`. It feeds `ID_83006` the raw ESM form, not the instance-derived form — a likely cause of the off-planet fault/mismatch. `[decompile-verified]` (src/Main.cpp:206-238; scan-component-lifecycle.txt:31-35)

### 3.3 `ID_83009` — the canonical READER (lookup-only)

`ID_83009` (`@1413079d0`): for a ref, if `ID_36022(base+0xc8,0x2a)==0`, looks up `(ID_939118 | refFormID<<16)` in `db+0x268` and returns **`entry+0x24`** (canonical). On a miss (`out[3]==0xfe0 && entry==0`) returns **`ref+0x28`** (the ref's own id). `[decompile-verified]` (verify-canonical-2026-06-21.txt:1-61)
- It does **NOT** re-run the `vtable[0x228]→ID_83006` derivation — it only **reads an already-stored** value. So the canonical value exists only if a prior `ID_83004` stamped it. `[decompile-verified]` (verify-canonical-2026-06-21.txt:17-42)
- On a bare PlaceAtMe'd instance with no ScannableComponent → returns the **own dynamic id** — reproduces **G3(b)** verbatim. `[in-game-confirmed]` (verify-canonical-2026-06-21.txt:35-37)

### 3.4 Sibling aggregators confirm the field semantics

`ID_83011` (`'2'`-tagged) and `ID_83013` (`'.'`-tagged) re-query `(ID_939118 | child+0x28)` and read **`entry+0x24`**, falling back to `child+0x28` on a miss — same canonical field, confirming canonical ids are stored per-component and read back by formId key. `[decompile-verified]` (scan-component-lifecycle.txt:139-269)

### 3.5 Component field map (per-instance `939118` record)

| Offset | Meaning | Read by | Set by |
|---|---|---|---|
| `+0x20` | authored/source id (low dword) | — | `ID_83004` |
| `+0x24` | **canonical id** | `ID_83009`, `ID_83011`, `ID_83013` | `ID_83004`, `ID_83043` (restore) |
| `+0x28` | scanned flag byte | `ID_83007` branch-A, `ID_83038` | `ID_83038`, `ID_83008` |

`[decompile-verified]` (scan-component-lifecycle.txt:65-66; verify-canonical-2026-06-21.txt:38-41; scan-inner.txt:136-137)

### 3.6 ⚠️ THE UNRESOLVED CRUX

**Is the canonical id STATIC (derivable from a base ESM form off-planet) or MATERIALIZATION-dependent (only correct via a live instance)?** Decompile shows the engine derives canonical via `vtable[0x228](LIVE instance) → ID_83006`, and `ID_83009` only *reads* an already-stored value. **No dump proves `ID_83006(rawEsmForm) == ID_83006(vtable[0x228](liveInstance))`.** The off-planet `ID_83006` path **FAULTED in-game (G3c)**, so it is neither proven static nor proven materialization-only. This single question gates whether off-planet ref-free green is achievable at all. `[inferred]` / **OPEN**

---

## 4. THE WRITE — which record, which key, which bytes, which events

### 4.1 The three containers (do not conflate)

| Container | Type | Discriminator(s) | Persisted? | Holds the green? |
|---|---|---|---|---|
| `db+0x268` (one record kind) | **BSGalaxy::PlayerKnowledge** (per-planet survey) | **`ID_938333`** | **YES (saved)** | **YES — `+0x21`** |
| `db+0x268` (another record kind) | **ScannableComponent** (per-ref) | **`ID_939118`** | transient, rebuilt per load | No (only intra-session "this instance scanned") |
| `db+0x3d8` | **DisposableInstancedFormDB** (RB-tree) | per-instance timestamp key | **rebuilt empty per load** | No |

`[decompile-verified]` (spoof-investigation.txt:333-379; COMPLETE-scan-to-green-trace-2026-06-21.txt:196-223; knowledge-api.txt:1302-1348)

- **`db+0x268` multiplexes MANY record types by the 16-bit discriminator** (also `ID_937885`, `ID_938337`, `ID_937674` seen), not just the named ones. `[decompile-verified]` (discriminators.txt:142-144, 536-560)
- The saved knowledge component is built by **`ID_52204`** = `CreateAndDeleteCommand<…, BSGalaxy::PlayerKnowledge>`. This is why `+0x21`/`+0x20` persist across save/restart. `[decompile-verified]` (knowledge-api.txt:1302-1348)

### 4.2 The composite key

64-bit key = `(discriminator << 48) | (id << 16)` — id in bits 16..47, discriminator in bits 48..63. Built as `CONCAT24(DISC, id) << 0x10` or `((DISC << 0x20) | id) << 0x10`. For survey: `DISC = ID_938333`, `id = planetId`. `[decompile-verified]` (knowledge-api.txt:12, 98, 291; green-render-path-2026-06-21.txt:60, 84)

### 4.3 The species sub-key & slot layout (per-planet `938333` entry)

- `subobj = entry+0x20`. Species hashmap at `subobj+0x18` (== `entry+0x38`); slot array at `subobj+0x40` (== `entry+0x60`), **0x30-byte stride**; sentinel at `subobj+0x48` (== `entry+0x68`). `[decompile-verified]` (knowledge-api.txt:768-778; COMPLETE-scan-to-green-trace-2026-06-21.txt:301-303)
- **Slot field map:** `slot+0x00` = species id; **`slot+0x20` = percent byte**; **`slot+0x21` = scan-flag (green) byte**; `slot+0x24` = a 4-byte data dword (read by `ID_52172`, written by `ID_52174`); `slot+0x28` = bucket next-link. `[decompile-verified]` (real-scan-chain.txt:651, 618-624; knowledge-api.txt:1024-1027, 1133-1136)
- **Sub-key = the bare 4-byte species formId**, hashed by **`ID_124901`** (FNV-1a; basis `0xcbf29ce484222325`, prime `0x100000001b3`, 4 key bytes, 0x30 stride). **Writer and renderer must hash the identical 4 bytes to hit the same slot.** `[decompile-verified]` (real-scan-chain.txt:709-740)

### 4.4 `ID_126806` — the `db+0x268` accessor

Binary search over a sorted array: 2-byte offset table at `base+0x12` (indexed `idx*4`), 8-byte comparison key at `element+0x10`. **Hit:** result `[+0x10]=base`, `[+0x18]=index`. **Miss:** `[+0x18]=0xfe0`, `[+0x10]=0` (the `0xfe0/0` sentinel every caller checks). `[decompile-verified]` (outline-decider.txt:599-645)
- ⚠️ **OPEN:** the higher-arity `ID_126806` call form (extra args at `ID_83038`/`ID_52172` sites) isn't explained by the 3-arg body; a ref-counted/lock-token overload may exist. The `db+0x3d8` accessor `ID_126802` has a **different** miss sentinel `0x7c`. `[decompile-verified]` for the discrepancy.

### 4.5 What the REAL scan writes (full effect of `ID_52158`)

Beyond `+0x21`/`+0x20`, a real scan via `ID_52158` does **all of §2.5**: the was-unscanned pre-read, threshold/percent compute, the `ID_101322` flag-set event, `subobj+0x00 |= 8`, the `ID_937609+0x160` current-biome propagation, the `ID_909826` sibling recursion, and (back in `ID_52157`) `ID_83019` + `ID_97853`. `[decompile-verified]` (real-scan-chain.txt:185-385, 98-101)

### 4.6 What the MOD's ref-free write does (and skips)

`MarkSpeciesScannedForPlanet(planetId, speciesFormId, delta)`:
1. `ResolvePlanetSubobj` → key `(ID_938333<<48)|(planetId<<16)`, `ID_126806`, `subobj = out[2] + *(u16*)(out[2]+0x12+out[3]*4) + 0x20`. `[decompile-verified]` (src/Main.cpp:251-270)
2. `IncrementScanFlag = ID_124898(subobj, speciesFormId, delta, 0)` → writes `+0x21` (+ `subobj|=8`). `[decompile-verified]`
3. `SetPercentByte = ID_124899(subobj, speciesFormId, 100, 0)` → writes `+0x20`. `[decompile-verified]` (src/Main.cpp:289-306)

It **SKIPS:** the was-unscanned pre-read, threshold/percent compute, `ID_101322`, the entire current-biome propagation, the `ID_909826` sibling recursion, `ID_83019`, and `ID_97853`. It writes the two persistent bytes + the dirty bit, nothing more. `[decompile-verified]` (src/Main.cpp:301-304)
- The mod also has `CompleteTypeForPlanet` which drives `ID_52158` directly with an explicit planetId (getting biome propagation + events) but requires a live spawned `ref` and an existing `db+0x268` entry, else `ID_52158` early-returns. `[decompile-verified]` (src/Main.cpp:334-363)

### 4.7 Events fired (and skipped by raw writes)

- `ID_52158` fires `ID_101322` on a newly-crossed threshold. `[decompile-verified]`
- `ID_52157` fires `ID_83019` (if notify) then always `ID_97853`. `[decompile-verified]`
- `ID_97853` (SurveyCheckNotify, `@1417daca0`): rebuilds progress via `ID_1016657`/`ID_65318`; if `(percentDelta>0 && category∉{0,0x0f})` dispatches `ID_64214()`-sourced (per-species progress); if `local_c8==local_c4` (planet complete) dispatches `ID_64213()`-sourced (planet-complete, drops the Survey Data slate). `[decompile-verified]` (survey-percent.txt:112-168; green-render-path-2026-06-21.txt:328-384)
- `ID_52209` returns the `BSTGlobalEvent::EventSource<BGSPlanet::PlayerKnowledgeFlagSetEvent>` singleton (the sink `ID_52153`/`ID_97853` publish to via `ID_123824`). `[decompile-verified]` (green-render-path-2026-06-21.txt:461-482)
- **Consequence:** a raw `+0x21` write that skips `ID_97853` leaves the engine believing the completion event never fired (no slate/XP/UI-complete transition). `[decompile-verified]`
- ⚠️ **OPEN:** `ID_83019`, `ID_101322`, `ID_64213`, `ID_64214`, `ID_123824` bodies are **not in any read dump**. Crucially, **what *subscribes*** to `PlayerKnowledgeFlagSetEvent` — whether any subscriber triggers an outline RE-RENDER or marks the save dirty — is **unestablished**. The render path reads `+0x21` fresh each frame, so green may need no event at all; the event's role is unproven.

---

## 5. PERSISTENCE — what is actually saved

- **Saved:** the per-planet `938333` PlayerKnowledge entry in `db+0x268` — including `slot+0x20` (percent), **`slot+0x21` (green flag)**, `slot+0x24` (data dword), `subobj+0x20` planet bits, and the `subobj+0x00` dirty bit set by `ID_124898`. The key is **deterministic** `(planetId, speciesFormId)` with **no instance/timestamp state**. `[decompile-verified]` + `[in-game-confirmed]` (COMPLETE-scan-to-green-trace-2026-06-21.txt:196-223; G1/G2)
- **NOT durably the green source:** the per-instance `939118` ScannableComponent (`+0x28` scanned byte) is transient and **rebuilt per load** by `ID_83043` (which re-stamps `entry+0x24` species id and `entry+0x20` state on restore). `[decompile-verified]` (scan-component-lifecycle.txt:295-410)
- **NOT persisted:** `db+0x3d8` DisposableInstancedFormDB (RB-tree at `subobj+0x60` via `ID_124902`, per-instance timestamp key minted by `ID_137052`, persisted via `ID_46756`) — **rebuilt empty each load**. `[decompile-verified]` (real-scan-chain.txt:744-816; knowledge-api.txt:894-901)
- **Why fresh instances render green on reload:** the green read is keyed on the persistent `(planetId, speciesFormId)` `938333` slot, **not** on any per-instance record. A new instance of the same species/base form hashes to the same slot and reads the same saved `+0x21`. `[inferred]` (mechanism) corroborated by `[in-game-confirmed]` G1/G2.

---

## 6. THE RENDER / READ DECISION — green vs blue

### 6.1 The outline deciders

- `ID_90491` reads the rendered form's id at `form+0x28` and calls **`ID_52159(ID_922868 /*PLAYER*/, *(u32*)(form+0x28))`**. The key **X = the raw FormID field at `form+0x28`** — **not** a separately-derived canonical id at this call site. `[decompile-verified]` (outline-pipeline-2026-06-21.txt:122; src/Main.cpp:116)
- `ID_90548` likewise calls `ID_52159(ID_922868, *(u32*)(lVar17+0x28))` and drives the outline-color byte `param_1[0xf2c] = result + 1`. It **also** calls `ID_83007` and stores its char result into the same `param_1[0xf2c]` under other conditions. `[decompile-verified]` (outline-decider.txt:428, 536, 144-145)
- ⚠️ **OPEN:** `ID_90491`/`ID_90548` raw bodies were summarized from the outline-pipeline trace, not independently re-read this pass.

### 6.2 `ID_52159` — the green reader (raw `+0x21`, no threshold)

`ID_52159(player, X)`:
1. `ID_52188(player, …)` resolves the **PLAYER's** current planet (planet binds at READ time to the player; at WRITE time to the scanned ref — for a normal scan they coincide). `[decompile-verified]` (discriminators.txt:698-744)
2. Keys `(ID_938333 << 0x20 | planetId) << 0x10` into `db+0x268` via `ID_126806`. `[decompile-verified]`
3. Hashes X via `ID_124901(subobj+0x38, &X)`; if found returns the byte at `*(subobj+0x60) + 0x21 + idx*0x30` — **the raw `+0x21` scan-flag byte, NO threshold compare.** `[decompile-verified]` (discriminators.txt:719-723; outline-decider.txt:133-136)
- `ID_90491`/`ID_90548` treat **any nonzero `+0x21` as green** (`bVar = cVar4 != '\0'`). `[decompile-verified]` — confirmed by **G1/G2** (`[in-game-confirmed]`, COMPLETE-scan-to-green-trace-2026-06-21.txt:178-184).

### 6.3 The membership fallback

When `ID_52159` returns false, `ID_90491` falls back to `ID_52180(iVar10, &local_48)` (a knowledge-DB-backed set of ids: `manager+0x8b0 → +0x238`, `ID_126655` + `ID_52181`) and sets `bVar12 = (X absent from the set)`. So the final boolean is **"green flag set OR id absent from the `ID_52180` set."** `[decompile-verified]` (outline-pipeline-2026-06-21.txt:122-139; green-render-path-2026-06-21.txt:1-46)
- ⚠️ **OPEN:** whether `ID_52180`/`ID_52181` key by canonical or authored id is unverified.

### 6.4 `ID_83007` is NOT the sole green decider (correcting prior verdicts)

`ID_83007` (`@1413076d0`) branches on `ID_36022(base+0xc8, 0x2a)`:
- **Branch A (clear):** looks up per-instance `(ID_939118 | ref+0x28)` ScannableComponent, returns `(byte_at_+0x28 != 0) + 1` (0=not-scannable, 1=scannable/unscanned, 2=this instance scanned). `[decompile-verified]`
- **Branch B (set):** dynamic-id → `ID_52162` (RB-tree membership read on `db+0x3d8`); static-id → `ID_52159(form, formId)`, returns `(result != 0) + 1`. `[decompile-verified]` (scanned-state.txt:26-114)
- It is the **shared per-ref scanned-state helper** used across the outline cluster (`ID_90548` ×2, `ID_90503/90506/90518/90536/90974/90983/90984`), which is why earlier verdicts mistook it for THE green decider. It feeds the outline byte under *some* conditions but is not the sole decider. `[decompile-verified]` (outline-callers.txt:1-18)

### 6.5 `ID_52162` — the membership/tree variant

`ID_52162` resolves a planet/system tree via `ID_126802(db+0x3d8, …)` then walks a red-black tree keyed on a 3-part id, setting an output bool. **Distinct** from the flat `+0x21` table read. (This is the *transient* dynamic-id/map-marker layer, **not** the persistent biome-flora/fauna green.) `[decompile-verified]` (species-scanned-check.txt:1-82)

### 6.6 Why "100% / BIOME COMPLETE but BLUE" (G3a) — and where the two readers diverge

- **Same byte, same key — for percent:** `GetSurveyPercent` aggregator `ID_97851` (`@1417da810`) walks the `938333` subobj slot table reading the **same `*(subobj+0x60)+0x21+idx*0x30` byte** via `ID_124901`, but **threshold-compares** it against `ID_69506`/`ID_69507` into four category counts; `ID_97850` (`@1417d9ea0`) then stores `numerator/denominator` as a float at `buf+0x1cc`. **Percent is driven purely by `+0x21`, with NO dependence on the green tree.** `[decompile-verified]` (survey-percent.txt:1-108; survey-compute.txt:572-583)
- **The divergence (the bug):** the render decider `ID_52159` hashes `*(u32*)(renderedForm+0x28)` = the **spawned base form's** formId, while a raw pre-write under the **authored ESM id** lands `+0x21` in a **different FNV slot**. The aggregator `ID_97851`, walking the planet's authored id arrays, still counts 100%. Result: **survey reads 100% / BLUE outline**. `[in-game-confirmed]` G3(a) + `[decompile-verified]` mechanism (COMPLETE-scan-to-green-trace-2026-06-21.txt:255-291).
- ⚠️ **PARTIAL DISAGREEMENT WITHIN THE EVIDENCE:** the "wrong species-key domain (a)" explanation is the decompile's *leading* hypothesis but the trace itself flags it **cannot fully exclude** mechanism **(c) entry-materialization timing** (the `938333` entry not existing / not yet created for the target planet, making `ID_52158`/`ResolvePlanetSubobj` no-op). Static analysis alone does not decide (a) vs (c); a guarded live retest is required. `[inferred]`

---

## 7. THE SURVEY-% READERS

- **`ID_97851`** (aggregator): per category, counts species whose `slot+0x21 >= ID_69506()/ID_69507()` threshold. `[decompile-verified]`
- **`ID_97850`**: sums weighted numerators/denominators, stores the float ratio at `buf+0x1cc`. This float is the UI's "BIOME COMPLETE / %". `[decompile-verified]` (survey-compute.txt:572-583)
- **`ID_47400`** classifies the species (`'.'`=flora→`ID_69507`, `'2'`=fauna→`ID_69506`) for both the writer (`ID_52158`) and this reader. `[decompile-verified]`
- **`ID_52172`** reads the per-planet survey-data dword: keys `(938333|planetId)`, and if `slot+0x20` low bit clear returns `slot+0x24` (else 4/0xffffffff); **`ID_52174`** writes `slot+0x24`. `[decompile-verified]` (knowledge-api.txt:1024-1027, 1133-1136)
- **`ID_52153`** is the planet-level trait/flag writer: maps category (0→1,1→2,2→3,3→4,6→0xe), keys `(938333|planetId)`, sets/clears bit `(1<<category)` in `slot+0x20`; on miss constructs the slot via `ID_52204`/`ID_51421`; ends by dispatching `ID_52209`-sourced event via `ID_123824` then `ID_97853`. `[decompile-verified]` (knowledge-create.txt:27-251)

**Net:** green (`ID_52159`, raw `+0x21`, no threshold) and survey-% (`ID_97851`/`ID_97850`, thresholded `+0x21`) **read the same byte from the same table by the same hash** — but the green read uses the **rendered instance's formId** as the key, whereas the percent aggregator walks the **planet's authored id arrays**. That asymmetry is exactly what produces "100% + blue" when a write lands under the wrong id domain. `[decompile-verified]` / `[in-game-confirmed]`

---

## 8. WHAT THE MOD WRITES TODAY (mapped to the engine path it imitates)

| Mod entry point | Engine path imitated | Effect |
|---|---|---|
| `CompleteSurvey()` (on-planet) | spawn one of each species → `SetScanned`=`ID_83008` + `UpdatePlanetProgressForSpecies`=`ID_52157` + `ScanNearbyRefs` | **Greens current planet, persists (G1)** `[in-game-confirmed]` |
| `UpdatePlanetProgressForSpecies` | `ID_52157(ref, speciesFormId-as-count, 0x0d, 0, 0)` | drives per-planet updater directly, bypassing `ID_83038`'s no-op on bare refs `[decompile-verified]` (src/Main.cpp:180-185) |
| `MarkResourcesForPlanet` | `WritePlanetSurveyState` = `SetPlanetAttributeBits \|0x7` on `subobj+0x00` + `MarkEverythingForPlanet` (`ID_1016657`) + `NotifySurveyProgress` (`ID_97853`) | ref-free DATA layer (bits + every `+0x21`/`+0x20` + slate event) `[decompile-verified]` (src/Main.cpp:974-987) |
| `MarkSpeciesScannedForPlanet` | `ID_124898` (+0x21) + `ID_124899` (+0x20) | same two persistent bytes the real scan sets, **but none of the events/propagation** `[decompile-verified]` |
| `GetCanonicalSpeciesId` | `ID_83009(ref,0,0,0)` | reads `+0x24` canonical, falls back to own id on miss (G3b) `[decompile-verified]` |
| `GreenSpeciesEverywhere` | `ScanRefNative`+`UpdatePlanetProgress` FIRST (to populate `+0x24`), THEN read `ID_83009`, fall back to ESM id, write `+0x21`/`+0x20` per host planet | the canonical-keyed F2 fix `[decompile-verified]` (src/Main.cpp:1089-1123) |
| `MarkEsmSpeciesForPlanet` | `CanonicalFormId(LookupByID(esmFid))` = `ID_83006(form)+0x28`, write `+0x21` under it (fall back to raw esm id when 0) | off-planet canonical write; wrapped in try/catch for the `ID_83006` AV `[decompile-verified]` (src/Main.cpp:525-545, 219-238) |
| Galaxy `CompleteAllPlanetsSurveyData` | per barren planet: `ID_102650(0,planetId,1)` (discover/create) + `WritePlanetSurveyState`; **skips living worlds entirely** (no green pass) | DATA-only, works ref-free from anywhere (G4) `[in-game-confirmed]` (src/Main.cpp:625-660) |
| `GreenAllPlanets` (separate command) | `EnumerateAllSpecies` → per species PlaceAtMe → `GreenSpeciesEverywhere` → Disable+Delete | per-species galaxy-green; **must be run on a planet**, NOT invoked by the data command `[decompile-verified]` |
| All native binds | `GuardedNative<>::call` (CPS_GUARDED, `/EHa`): catches C++ exc OR AV, logs `[native] caught`, latches `Engine::g_degraded` for the session; `CanonicalFormId` has an extra local try/catch for the `ID_83006` AV (G3c) | turns the off-planet crash into per-species degradation `[decompile-verified]` (src/Main.cpp:775-809) |

---

## KEY STRUCTS / OFFSETS / IDs (with confidence)

**Engine functions (all `[decompile-verified]` unless noted):**
- `ID_118472` SetScanned Papyrus impl · `ID_83008` SetScanned inner (category `0xd`) · `ID_83007` is-scanned gate · `ID_83038` find-only flora writer
- `ID_52157` per-planet progress updater · `ID_52158` count/percent writer · `ID_52188` planet resolver (ref-location based) · `ID_52160` fauna/actor wrapper
- `ID_124898` `+0x21` saturating mutator · `ID_124899` `+0x20` percent writer · `ID_124901` FNV-1a hash probe (0x30 stride) · `ID_124840` slot creator · `ID_124902` RB-tree insert (db+0x3d8)
- `ID_83004` ScannableComponent creator (canonical via vtable[0x228]→`ID_83006`) · `ID_83006` form-level canonical resolver (**faults off-planet, G3c**) · `ID_83009` canonical reader (lookup-only) · `ID_83043` per-instance save-restore · `ID_64338` `ID_83006` gate · `ID_63393` `ID_83006` default · `ID_47401` formid→form
- `ID_36022` bit-test (`0x2a` flora/actor discriminator; `0x2b` in `ID_64338`) · `ID_47400` species classifier (`'.'`/`'2'`)
- `ID_90491`/`ID_90548` outline deciders (bodies `[inferred]`-summarized) · `ID_52159` green reader (raw `+0x21`) · `ID_52162` RB-tree membership read · `ID_52180`/`ID_52181` known-id set fallback
- `ID_97851`/`ID_97850` survey-% aggregator · `ID_97853` SurveyCheckNotify · `ID_52172`/`ID_52174` survey-data dword R/W · `ID_52153` trait/flag bit writer · `ID_52204` PlayerKnowledge CreateAndDeleteCommand · `ID_51421`/`ID_37348` entry init/teardown
- `ID_126806` `db+0x268` accessor (miss `0xfe0/0`) · `ID_126802` `db+0x3d8` accessor (miss `0x7c`)
- `ID_102650`/`ID_102651` orbital scan-complete (planet bit-4 set, recursive)
- `ID_69506`/`ID_69507` thresholds (bodies **not read**, `[decompile-verified]` only as call) · `ID_101322`/`ID_83019`/`ID_64213`/`ID_64214`/`ID_123824` events (bodies **not read**)

**Discriminators (16-bit, into `db+0x268`):** `[decompile-verified]`
- **`ID_938333`** — per-(planet,species) **survey/PlayerKnowledge** (holds the **green** `+0x21`, percent `+0x20`, data `+0x24`, planet bits `subobj+0x20`)
- **`ID_939118`** — per-ref **ScannableComponent** (canonical `+0x24`, scanned `+0x28`; transient)
- `ID_937885` — per-form discovery-state (single formId key, `slot+0x20` small code)
- `ID_938337`, `ID_937674` — further record types (semantics unestablished)
- ⚠️ **OPEN:** whether the canonical-species map (`939118`) and the survey/percent map (`938333`) are the SAME hashmap under different discriminators or distinct — affects whether writing under one id touches the other.

**Survey slot layout (`938333` entry):** `[decompile-verified]`
```
entry+0x20 = subobj
subobj+0x18 = species BSTHashMap   (== entry+0x38)   [ID_52159 reads subobj+0x38]
subobj+0x40 = slot array base      (== entry+0x60), stride 0x30
subobj+0x48 = sentinel             (== entry+0x68)
subobj+0x00 |= 8  -> dirty/known bit (set by ID_124898)
subobj+0x20: planet attribute bits (bit4 orbital, bits|0x7 data sweep)
  slot+0x00 = species id
  slot+0x20 = PERCENT byte      (ID_124899 write / ID_97851 threshold read)
  slot+0x21 = SCAN-FLAG/GREEN   (ID_124898 saturating write / ID_52159 raw read / ID_97851 threshold read)
  slot+0x24 = data dword        (ID_52174 write / ID_52172 read)
  slot+0x28 = bucket next-link
```

**ScannableComponent layout (`939118` entry):** `+0x20` authored id · `+0x24` canonical id · `+0x28` scanned byte. `[decompile-verified]`

**Hash:** FNV-1a, basis `0xcbf29ce484222325`, prime `0x100000001b3`, over the **4-byte species formId**. `[decompile-verified]`

**Key constants:** category `0x0d` (kBiomeScanCategory) · sibling-recursion category `0x0b` · `ID_500678`=100.0 · `form+0x28`=formId field (kFormPtrFormIdOffset). `[decompile-verified]`

**Globals:** `ID_922868` = player · `ID_937609+0x80` = current-planet global · `ID_937609+0x160` = engine current-biome (NOT the write target) · `ID_909826` = sibling-recursion marker form (identity unestablished). `[decompile-verified]` (identity of `ID_909826` `[inferred]`)

---

## OPEN QUESTIONS / NOT ESTABLISHED (consolidated)

**A. The canonical-id crux (highest-priority, gates everything off-planet):**
1. **Static vs materialization-dependent canonical id.** No dump proves `ID_83006(rawEsmForm) == ID_83006(vtable[0x228](liveInstance))`. The off-planet path **faulted (G3c)**, so it is neither proven static nor proven materialization-only. **The whole green bug hinges on this.**
2. **What `vtable[0x228]` is** (the instance→base accessor `ID_83004` calls before `ID_83006`). Looks like a `GetBaseObject`/`GetTemplateBase`; concrete vtable identity for biome flora/fauna unestablished. `ID_47327` also calls `(*(p+0x228))()`.
3. **What `vtable[0x428]` resolves to** (called via `ID_44958` inside `ID_83006`) and whether it is species-stable across leveled/biome variants.
4. **Whether authored (`+0x20`) and canonical (`+0x24`) ids actually DIFFER** for the specific failing biome fauna. Asserted for leveled/template fauna (src/Main.cpp:190-194) but **no dump shows a concrete `+0x20 != +0x24` pair**. Needs a runtime dump of a real stored ScannableComponent.
5. **Whether `ID_64338`'s gate** (`+0x98` non-null, `+0x2e=='2'`, `0x2b`/`ID_47225`) is satisfied by a never-materialized ESM form or only by an on-planet materialized/leveled instance. The `'2'` tag and `0x2b` checks are opaque (no form-type-byte legend).
6. **`ID_83006`'s off-planet semantics are unproven, not confirmed-wrong (G3c).** It crashed before logging. The mod's local try/catch degrades it to "no canonical" → falls back to raw esm id → **re-introduces the G3a blue** for any form where `ID_83006` faults. Net effect on those species unverified.

**B. The blue-vs-100% mechanism is not fully decided by static analysis:**
7. **(a) wrong-key-domain vs (c) entry-materialization-timing** for the G3a blue. The trace points hardest at (a) but **cannot exclude (c)**. Needs a guarded live retest.
8. **Whether a `+0x21` write to a NEVER-VISITED planet succeeds.** `ResolvePlanetSubobj`/`ID_52158` early-return when the `938333` entry is absent. The galaxy data command pre-creates barren entries via `ID_102650`; the green path (`GreenSpeciesEverywhere`) does **NOT** pre-create living-world entries before writing — so off-current-planet green writes may **silently no-op**.
9. **The canonical-key F2 fix is decompile-predicted but NOT in-game re-tested.** No dump records a previously-blue planet turning green with the canonical key. Only the barren-planet DATA sweep (G4) is in-game-confirmed.

**C. Threshold / classifier details:**
10. **`ID_69506`/`ID_69507` bodies** (how the threshold derives from the instant-scan GMSTs / player skill `ID_922868`) not read. Comparison verified; derivation not.
11. **`ID_47400`'s `'.'`/`'2'`/const-1 → category mapping** not decoded; affects `+0x20` percent but not the `+0x21` green gate.
12. **`ID_36022` bit `0x2a` authored source** on the base record not verified; flora=clear/actor=set is `[inferred]`.

**D. Events & their subscribers (render-refresh? save-dirty?):**
13. **`ID_83019`, `ID_101322`, `ID_64213`, `ID_64214`, `ID_123824` bodies not in any dump.** Crucially, **what subscribes to `PlayerKnowledgeFlagSetEvent`** — whether any subscriber re-renders the outline or marks the save dirty — is unestablished. Render reads `+0x21` fresh each frame, so green may need no event; the event's role is unproven. Whether `ID_83019` or `ID_101322` (vs neither) seeds any persistent per-type "surveyed" flag is open.
14. **Sibling recursion gate `ID_909826`** identity and whether the recursion fires for normal flora/fauna (vs only aggregate species) unestablished. `ID_83052`/`ID_83059` exact returned sibling list not read.

**E. Read-path / planet-binding details:**
15. **Which `ID_52188` path wins at render** on a freshly-loaded planet (ExtraLocation `0x81` vs parentCell vs `ID_937609+0x80` global) — green planet-binding correctness depends on this, not independently confirmed.
16. **`ID_52188` on a bare PlaceAtMe'd ref** (returns current planet or fails?) — both code paths read, neither confirmed in-decompile. Ground truth says it credits the planet you stand on.
17. **`ID_90491`/`ID_90548` raw bodies** not independently re-read (summarized from outline-pipeline) — their call to `ID_52159(player,species)` and the `ID_52180` fallback not re-confirmed this pass.
18. **`ID_52159` offset `+0x21` vs `+0x20`** verified from one dump, **not cross-build-checked** against a second independent `ID_52159` decompile.
19. **`ID_52180`/`ID_52181` key domain** (canonical vs authored) for the membership fallback unverified.

**F. Container internals:**
20. **`ID_126806` variadic/higher-arity overload** (extra args at `ID_83038`/`ID_52172` sites) not explained by the 3-arg body — possible lock-token/ref-counted overload.
21. **BSComponentDB2 `ComponentKey<SortByTypeTraits>` field layout** only partially reconstructed from `ID_126806`.
22. **`ID_137052` (timestamp key mint) / `ID_46756` (persist into `db+0x3d8`)** referenced but not read; DisposableInstancedFormDB catalog key format not byte-confirmed.
23. **`ID_938337`/`ID_937674` record semantics** (what each stores, which system consumes them) unestablished.

**G. Per-instance vs per-planet ordering:**
24. **`ID_38418`** (the insert primitive `ID_83004`/`ID_83043` use to create the `939118` entry with the `+0x24` stamp) body not read — exactly when the `+0x24` stamp commits before a scan can write `+0x28` is unconfirmed. On the per-PLANET `938333` subobj there is **no** separate `+0x24` stamp before `+0x21` (the slot auto-creates), so the "+0x24-before-+0x21" question applies only to the per-instance `939118` component, not the survey subobj. `[decompile-verified]`

**H. Superseded prior verdicts (recorded so they are not resurrected):** `[in-game-confirmed]`
- `green-spoof-verdict` / `extradata-0x81-spoof-verdict`: "green is per-ref ScannableComponent `939118`+0x28, planet-agnostic / per-species galaxy-wide" — **WRONG** (G7: green is per-(planet,species)). Both carry SUPERSEDED banners.
- `green-verdict` (Q4): "ref-free green impossible, outline keys on per-instance timestamp catalog (`ID_137052`/`db+0x3d8`)" — **WRONG for biome flora/fauna** (that path is the transient dynamic-id/map-marker layer; the biome decider is `ID_90491`/`ID_90548 → ID_52159` reading `+0x21`). SUPERSEDED.
- `green-persistence-verdict`: "`+0x21` alone = green" — **false AS POSED** (G3a: wrong key domain → 100% but blue), even though `+0x21` IS the byte `ID_52159` honors.
