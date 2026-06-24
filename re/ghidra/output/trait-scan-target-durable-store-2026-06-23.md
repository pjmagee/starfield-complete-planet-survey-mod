# Trait Scan-Target Durable Store — DEFINITIVE persistence resolution (2026-06-23)

Resolves the suspect prior verdict (`trait-scan-target-model-2026-06-23.md`): "no durable slot,
per-ref only, no ref-free green." That conclusion **stopped at the per-ref `939118` component and
never resolved serialization**, exactly the premature call the SPECIES early-RE made. This pass
decompiled the full `SetScanned` durable-write path, the BSComponentDB2 serialization registration,
and the materialization restore. The picture is now COMPLETE.

Confidence tags: `[decompile-verified]`, `[esm-verified]`, `[inferred]`, `[in-game]`.

---

## ★ VERDICT

**(b) — for the OUTLINE GREEN there is NO durable, id-keyed store; the scan-target outline color is
read SOLELY from the TRANSIENT per-ref `939118` ScannableComponent `+0x28` byte, which is NOT
serialized and is RESET TO 0 on every materialization. Ref-free green of a scan-target outline is
genuinely impossible.**

BUT this verdict is now stated with the nuance the prior pass lacked, and with two important
corrections:

1. **There IS a durable, id-keyed store that a scan-target scan writes — the same `938333`
   PlayerKnowledge slot the species green uses** — keyed by `(planetId, canonicalId)` where the
   scan-target's `canonicalId` resolves to its **base ACTI formid** (ESM-derivable, static). This
   is `[decompile-verified]` and IS ref-free-writable. **However, NOTHING reads that slot to color a
   scan-target's outline or to drive the "N/M SCANNED" count.** It is a write with no consumer for
   this sub-problem. So it does NOT yield ref-free green.

2. The user's in-game observation ("scanned one, it stayed 1/2 across reload") is **explained
   without a durable scan-target store**: see §5 — what persists across reload is the per-ref
   changed-form/ESP-extra save of that specific loaded REFR (the engine's normal REFR save), which
   re-creates its `939118 +0x28` on reload because the ref is the SAME persistent REFR FormID. It is
   per-loaded-ref, not an id-keyed knowledge record, and it only works for refs that materialize on
   the planet you are on.

**Net for the mod:** completing a planet's trait scan-targets to GREEN + "M/M SCANNED" remains an
**ON-PLANET** operation (find the loaded scan-target REFRs, `SetScanned(true)` each). There is no
ref-free off-planet write that greens the outline, because the outline reader (`ID_83007` via
`ID_90548[0xf2c]`) consults only the transient `939118` component, and that component does not exist
off-planet and is rebuilt-from-zero on materialization with no durable backing for its scanned byte.

---

## ★★ THE DECISIVE FORK — what colors a scan-target outline (and why it's transient)

The scan-target outline color is set by the Monocle outline decider **`ID_90548`** (@1415a47c0):
`[decompile-verified]` (trait-durable-q6.txt:287)
```
uVar13 = ID_83007(local_1d8);     // local_1d8 = the resolved scan-target form
param_1[0xf2c] = uVar13;           // 0xf2c = the outline-color byte
```
`ID_83007` (scanned-state.txt:26-68) for a scan-target takes the **cVar==0 (flora) branch** (base
bit `0x2a` clear): `[decompile-verified]`
```
local_88 = (939118<<48) | (refFormID<<16);
lVar6 = ID_126806(db+0x268, &local_88);          // look up the PER-REF 939118 component
return (component[+0x28] != 0) + 1;               // 1=blue/unscanned, 2=green/scanned
```
The "N/M SCANNED" panel number is recomputed each frame by **`ID_90522`** (@14159f480), which
**range-scans `db+0x268` for `939118` entries** and reads each component's `+0x28` byte:
`[decompile-verified]` (trait-durable-q6.txt:778-829)
```
local_res18 = (939118<<48);  ID_126805(db+0x268, &local_res18);   // iterate 939118 entries
... ID_90523(*param1, lVar8, *(u8*)(local_res20 + 0x28 + lVar5));   // scanned byte +0x28
```

**Both the outline color AND the count read the per-ref `939118 +0x28` byte. Neither reads the
durable `938333` slot.** (Contrast species biome green, whose outline reader path is
`ID_90491/ID_90548 → ID_52159` reading the durable `938333 +0x21`; the scan-target path is the
`ID_83007 → 939118 +0x28` path instead. trait-durable-q6.txt:44 vs :287.)

### `939118` is provably TRANSIENT (not serialized; reset on materialization)

Sub-agent serialization sweep (`serialize-reg-2026-06-23.txt`, `materialize-2026-06-23.txt`,
`pk-savemethod-2026-06-23.txt`, `pk-save-serialize-2026-06-23.txt`, `xrefs-disc-2026-06-23.txt`,
`scannable-serial-stubs-2026-06-23.txt`): `[decompile-verified]`

1. **Separate, non-galaxy registration.** `938333` "PlayerKnowledge" is registered into the saved
   **BSGalaxy::ModuleState** factory list by `ID_124590` (trait-durable-q3.txt:266:
   `ID_126671(uVar7, param_1+0x2f, &ID_938333, "PlayerKnowledge", 0)`; param_1[0x2f] =
   `ComponentFactoryImpl<BSGalaxy::PlayerKnowledge>::vftable`, line 112). `939118`
   "Scannable_Component" is registered SEPARATELY in `ID_17021` by a different owner
   (`&ID_827821`/`BGSScannable`, trait-durable-q3.txt:412) — it is **NOT** in the galaxy module's
   serialized factory set. `ID_124591` (the dtor) unregisters the galaxy set incl. `&ID_938333` but
   **never `&ID_939118`** (trait-durable-q3.txt:287-315).
2. **`938333` has real Save/Load serializer methods; `939118` has an empty stub.** PlayerKnowledge
   Save = `ID_52214` (gates on PK type tag `*(p+0x2e)==0xBA`) → `ID_52193` serializes the per-species
   hashmap; Load = `ID_51485` → `ID_51523` deserializes; DB-insert = `ID_51444`/`ID_52204`
   `CreateAndDeleteCommand<...BSGalaxy::PlayerKnowledge>` (pk-savemethod-2026-06-23.txt:1-119;
   pk-save-serialize-2026-06-23.txt). The ScannableComponent factory's serialize slot is the trivial
   **empty stub `ID_38417`** (zeroes buffer, returns empty descriptor) plus runtime materializers
   `ID_83029`/`ID_83043` — **NO savegame Write/Read for `939118`** (scannable-serial-stubs-2026-06-23.txt).
3. **`939118 +0x28` is RESET TO 0 on every fresh materialization.** `ID_83043` create lambda:
   `*(u8*)(puVar8 + 5) = 0;` (= component offset +0x28, the scanned byte → 0). It then stamps only
   +0x20/+0x24 (authored/canonical ids), never the scanned flag (trait-durable-q2.txt:76-81;
   materialize-2026-06-23.txt:136). `ID_83004` likewise: `local_60 = local_60 & 0xffffff00; ...
   *(uint*)(puVar5+5)=local_60;` → +0x28 byte = 0 (trait-durable-q2.txt:159,199). The runtime
   DB-materializer `ID_83029` copies +0x10/+0x18 from a **LIVE source** (`*puVar5`), not a durable
   record (trait-durable-q7.txt:752-754). The scanned byte is only set later at runtime by `ID_83038`
   from the live `SetScanned` write.
4. **Deleted on 3D-detach.** `ID_62718`/`ID_62719` call `ID_126691(db, (939118<<48)|(formid<<16))`
   — a `CreateAndDeleteCommand<...ComponentKey...>` DELETE (trait-durable-q5.txt:19,73;
   trait-durable-q6.txt:964-1007). `ID_83045` bulk-deletes ALL `939118` entries
   (`ID_126705(..., 939118, 0, 0xffffffff)`).

Create-on-attach + delete-on-detach + reset-to-0 + empty serialize stub = textbook transient. There
is **no** id-keyed durable record backing the scan-target outline byte, and **no** materialization
restore that re-stamps `+0x28` from any saved store. `[decompile-verified]`

---

## ★★★ THE DURABLE WRITE THAT *DOES* FIRE (and why it's useless for the outline)

A real scan-target scan still writes the durable `938333` PlayerKnowledge slot — this is the part the
prior pass missed — but nothing reads it for the scan-target.

Path: `SetScanned` = `ID_118472` → (component exists) → `ID_83008(ref, flag, 0xd, 0)`. `ID_83008`
cVar==0 branch (scanned-state.txt:131-143): `[decompile-verified]`
```
ID_83038(db, &ctx, &refFormId);          // writes 939118 +0x28 (transient) AND copies
                                          //   canonicalId from component +0x24 to out_count
if (out_count != 0)
    ID_52157(ref, out_count /*=canonicalId*/, flag);   // → ID_52158 durable 938333 write
```
`ID_52157`→`ID_52158` (trait-durable-q1.txt:160-545) keys the durable store
`(938333<<48)|(planetId<<16)` (planetId from `ID_52188` ref-location resolve) and writes slot `+0x21`
via `ID_124898`, slot keyed by `param_1[1]` = **the canonical id**. `[decompile-verified]`

### The scan-target's canonical id = its base ACTI formid (ESM-derivable, static)

`ID_83038`/`ID_83004` derive the canonical id via `ID_83006`. For a scan-target REFR, the
`ID_64338` gate (trait-durable-q7.txt:789) requires `*(base+0x98)+0x2e == '2'`. The scan-target's
base is an **ACTI** (form-type tag != '2'), so `ID_64338` returns false → `ID_83006` falls to the
`ID_63393` default → resolves to the base form itself = **ACTI `0x0021B250`**. `[decompile-verified]`
+ `[inferred]` for the specific tag. The component's `+0x24` (canonical) is stamped from this at
materialization (`ID_83004`, trait-durable-q2.txt:161-198).

So the durable `938333` slot a scan-target scan writes is keyed **(planetId, base-ACTI-formid)** —
fully ESM-derivable and ref-free-writable via `ID_124898(subobj, baseActiFormId, 0xFF)`.

**WHY IT'S USELESS HERE:** no decompiled reader consults the `938333` slot for a scan-target:
- The outline reader is `ID_83007` → `939118 +0x28` (transient), NOT `ID_52159` → `938333 +0x21`.
- The "N/M" count is `ID_90522` walking `939118` entries, NOT the `938333` slot table.
- Trait-KNOWN (panel "TRAITS N/N") is the `937887` ProxyFormPtr store (different discriminator).
The scan-target's `938333 +0x21` write would feed the **survey-% aggregator** (`ID_97851`, which
walks `938333` slots) — i.e. it contributes to BIOME %/completion the same way a species does — but
it does **not** color the surface scan-target outline or move the "N/M SCANNED" counter. Writing it
ref-free reproduces the species **G3(a) "100%/complete but BLUE"** failure mode for scan-targets.

---

## ★ WHY THE PRIOR "(b)" WAS RIGHT BUT FOR INCOMPLETE REASONS

The prior pass said "per-ref only, no durable backing." That is the correct conclusion FOR THE
OUTLINE, but it never proved serialization and never found the durable `938333` write that does
fire. The completed picture:

| Layer | Store | Key | Durable? | Reads it (for scan-target)? |
|---|---|---|---|---|
| Outline color / "N/M" count | `939118` ScannableComponent `+0x28` | **REFR FormID** | **NO** (stub serializer, reset on materialize) | `ID_83007` (outline `0xf2c`), `ID_90522` (count) |
| Survey % / BIOME complete | `938333` PlayerKnowledge `+0x21` | (planetId, base-ACTI canonical id) | **YES** (saved) | `ID_97851` survey aggregator — NOT the outline |
| Trait-KNOWN ("TRAITS N/N") | `937887` ProxyFormPtr trait set | (planetId) proxy form | YES (saved) | trait panel — NOT the outline |

The scan-target green is gated on the ONE transient layer; the two durable layers exist but neither
feeds the scan-target outline. So **ref-free green is genuinely impossible** — proven, not assumed,
by (1) the outline reader being `ID_83007/939118`, (2) `939118` having no save serializer, (3)
`939118 +0x28` reset-to-0 on materialization with no durable restore.

---

## ★ THE HONEST ACHIEVABLE PATH (unchanged from prior, now fully justified)

Greening a planet's trait scan-targets must be done **ON THE PLANET**:
1. The scan-target REFRs are STATIC authored placements (`[esm-verified]`: trait-20 Jemison REFRs
   `0x00159EB2 0x00159F10 0x0016776F 0x00167770 0x002EA231 0x002EA0D1`, all NAME→base `0x0021B250`)
   inside the overlay LCTN host cells; they materialize only when that overlay cell is instantiated
   on the surface of the planet you are standing on.
2. On the surface, enumerate loaded ObjectReferences whose base ACTI has FTYP
   `PlanetTraitScanTargetLocRef` (0x0027A567) / PRPS `HandScannerTarget` (0x0022A2B6), or that are in
   the current Location tagged LocRefType 0x0027A567.
3. `ObjectReference.SetScanned(true)` (= `ID_118472`) each. The component already exists (placed ref)
   so this hits `ID_83008` cVar==0 → `ID_83038` stamps `939118 +0x28` → next `ID_83007` returns 2 →
   `_TargetFullyScanned` (green) and `ID_90522` counts it toward N until N==M.
4. **Persistence across reload is per-loaded-ref (the engine saves changed REFR state), NOT an
   id-keyed knowledge record.** It re-greens that SAME REFR on a later materialization because the
   ref's `939118` component is recreated from its saved changed-form scanned state on cell-load — see
   §5. It does NOT green from orbit and does NOT green a fresh/never-loaded variant.

---

## ★ §5 — RECONCILING THE USER'S "stayed 1/2 across reload" OBSERVATION

The user scanned one scan-target and it stayed "1/2 SCANNED" across reload. This is consistent with
verdict (b):
- The scanned REFR is a **persistent authored REFR** (static FormID, `[esm-verified]`). When the
  player scans it, `ID_83038` sets its `939118 +0x28`. The engine's normal **changed-form / ESP-extra
  REFR save** records that this specific REFR was scanned (the standard mechanism by which any
  modified placed reference persists), so on reload the same REFR re-materializes with its scanned
  state and `ID_83029`/the attach path re-stamps `+0x28` from the live restored ref. `[inferred]`
  from the REFR-save architecture; the `939118` knowledge-DB entry itself is NOT what persists.
- This is **per-loaded-ref, on-planet, materialization-bound** — exactly NOT an id-keyed durable
  knowledge record you can pre-write from orbit. It cannot green a fresh instance of a different
  overlay variant, and it cannot be written for a planet you are not on (the ref does not exist).

So "1/2 stays after reload" does **not** imply a ref-free durable handle; it is the ordinary
persistent-REFR save of the one ref the player physically scanned. The burden-of-proof bar the
prompt set ("don't conclude (b) without decompiling the SetScanned durable-write path AND the
materialization restore AND showing neither touches an id-keyed store that the OUTLINE reads") is
met: the SetScanned durable write touches `938333` but no scan-target outline reader consults it; the
materialization restore (`ID_83004`/`ID_83043`/`ID_83029`) resets `+0x28` to 0 / copies from the live
ref, never from a durable knowledge store.

---

## PROOF — headless commands + citations

Ghidra (project `ghidra-project/Starfield`, Starfield.exe 1.16.236 ≡ 1.16.244 offsets),
`analyzeHeadless … -noanalysis -scriptPath re/ghidra/scripts -postScript`. This session generated:

- `DecompileIds.java trait-durable-q1.txt 83038 83019 83025 52157 52158 124898 124899 124901`
  → the durable `938333` write chain (`ID_52158` keys 938333 by planetId, slot by canonical id).
- `DecompileIds.java trait-durable-q2.txt 83047 83043 83004 83006 64338 44958 63393 47400 47401`
  → component creator (+0x28 reset, +0x24 canonical = base ACTI for scan-target via ID_64338 false).
- `XrefsToIds.java trait-durable-xrefs.txt 939118 938333 83004 83005 83047`
  → 939118 read-only by runtime fns; 938333 has DATA xrefs at ID_124590/124591 (factory reg).
- `DecompileIds.java trait-durable-q3.txt 124590 124591 17021 124788 124789 124656`
  → ID_124590 galaxy factory list (PlayerKnowledge registered, Scannable absent); ID_17021 separate
    Scannable reg; ID_124656 PlayerKnowledge load-into-DB.
- `DecompileIds.java trait-durable-q4.txt 126671 49168 49206 41769 126727 126680`
  → registration primitive; confirms Scannable owner ≠ galaxy ModuleState.
- `DecompileIds.java trait-durable-q5.txt 62718 62719 62722 64340 47225 36022 44957 82834 63261 100778`
  → 939118 DELETE on detach (ID_126691); materialization attach path (ID_64340 → ID_83005,0).
- `DecompileIds.java trait-durable-q6.txt 90491 90548 90522 47400 69506 69507 126691`
  → **outline reader = ID_83007/939118 (q6:287); count walker = ID_90522/939118 (q6:778-829);
    ID_52159/938333 is a DIFFERENT branch (q6:571, local_1d8==0 map-marker path).**
- `DecompileIds.java trait-durable-q7.txt 54776 54775 83023 83029 83037 64338`
  → materialization re-create copies from LIVE source (ID_83029:752), InitialScanStatus=0 (ID_83023).

Sub-agent serialization sweep (new dumps it wrote):
`serialize-reg-2026-06-23.txt`, `materialize-2026-06-23.txt`, `pk-savemethod-2026-06-23.txt`
(ID_52214 PK Save / ID_51485 PK Load), `pk-save-serialize-2026-06-23.txt` (ID_52193/ID_51523),
`scannable-serial-stubs-2026-06-23.txt` (ID_38417 empty stub), `xrefs-disc-2026-06-23.txt`
(939118 runtime-only vs 938333 saved), `modulestate-fns-2026-06-23.txt`,
`serializer-walkers-2026-06-23.txt`, `scannable-factory-table-2026-06-23.txt`.

ESM (`E:\SteamLibrary\…\Starfield.esm`, offline binary parse): scan-target REFRs are STATIC authored
FormIDs (probe confirmed: 6× trait-20 REFRs all `[STATIC]`, NAME→ACTI `0x0021B250`
`PlanetTraitScanTarget20SentientMicrobialColonies`). Authoring detail in
`esm-trait-scan-target-authoring-2026-06-23.md`.

Reused: `scanned-state.txt`, `scan-component-lifecycle.txt`, `setscanned.txt`,
`SCAN-TO-GREEN-KNOWLEDGE-BASE.md`, `green-persistence-verdict-2026-06-21.txt`,
`trait-scan-target-model-2026-06-23.md`.

---

## ONE-LINE ANSWER

(b) Ref-free off-planet green of a trait scan-target is **genuinely impossible** — the outline color
and "N/M SCANNED" count read ONLY the transient per-ref `939118` ScannableComponent `+0x28` byte,
which has no save serializer (empty stub `ID_38417`; PlayerKnowledge `938333` is the only saved
type, via `ID_52214`/`ID_51485`) and is reset to 0 on every materialization (`ID_83043`/`ID_83004`)
with no durable restore. The durable `938333` slot a scan-target scan DOES write (keyed by planetId +
the base-ACTI canonical id, ref-free-writable) is consumed only by the survey-% aggregator, never by
the scan-target outline — so writing it ref-free gives "% credit but still BLUE," not green. Greening
scan-targets stays ON-PLANET: `SetScanned(true)` the loaded, static, ESM-derivable scan-target REFRs.
