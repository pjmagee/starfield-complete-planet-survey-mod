# Trait Scan-Target — TRUE Completion Mechanism (re-derived from scratch, 2026-06-23)

Anchored on the IN-GAME FACT (it outranks every prior decompile claim):
the mod found the on-surface scan-target REFRs (`FindAllReferencesWithKeyword(0x001CBEA3)` /
`FindAllReferencesOfType(0x0021B250)`), called Papyrus `SetScanned(true)` on them, `IsScanned()`
then returned TRUE — **yet the aimed-at scan target stayed "UNKNOWN FEATURE 0/2 SCANNED" and BLUE.**

This pass re-decompiled the whole path (real hand-scan handler `ID_90506`, the Papyrus
`SetScanned`/`IsScanned` natives, `ID_83007/83008/83038`, `ID_52157/52158`, `ID_90548` panel-state
setter, `ID_90518` panel populate, the `938333` writers/readers). Confidence tags:
`[decompile-verified]`, `[esm-verified]`, `[in-game]`, `[inferred]`.

Headless dumps produced this session (project `ghidra-project/Starfield`, 1.16.236≡244):
`tc-branch-2026-06-23.txt` (36022/90522/90523/90548/90491/52159/52162/52188/83040/83041/83039/83009/83015),
`tc-q2-2026-06-23.txt` (124900/124901/124898/83006/63393/64338/52158/52157/124899/83005/47401/47400),
`tc-q3-2026-06-23.txt` (90506/90521/90676/90661/64292/64290/69506/69507/97853/97851/64337),
`tc-q4-2026-06-23.txt` (118497 native table / 83004/83043/83023/83029/64340 materializers / 118370 IsScanned),
`tc-q5-2026-06-23.txt` (118370/64337/64271/…), `tc-q6-2026-06-23.txt` (name-reveal helpers),
`tc-xrefs-2026-06-23.txt` (callers of 52157/83008/83005/118472/52158/90518/90522/97853/64292/64290).

---

## ★ ONE-LINE ANSWER

The scan-target's **count, identity reveal, and green ALL ride on the per-ref transient
`939118` ScannableComponent `+0x28` byte being set by the REAL scan handler `ID_90506`** —
**not** by Papyrus `SetScanned`. Papyrus `SetScanned(true)` (`ID_118472→ID_83008→ID_83038`) sets
`939118 +0x28` on the ref **you pass it** so `IsScanned()` reads back TRUE, but (a) it operates on
the *array of placed REFRs the mod enumerated*, which is **a different ScannableComponent instance
than the one the Monocle/aim path resolves and renders**, and (b) it does **not** run the
identity-reveal/count/notify side-effects the real scan runs. So the find-refs + SetScanned approach
sets a byte nobody is looking at. **Completion of a scan target is therefore materialization-bound
and aim-bound: the only thing that greens/counts/identifies it is the engine's own scan input on the
loaded, aimed reference. There is NO ref-free durable store you can pre-write to complete it.** The
durable `938333` PlayerKnowledge slot a real scan also writes (keyed by planetId + the base-ACTI
canonical id) is consumed only by the survey-% aggregator (`ID_97851`) and never by the scan-target
outline / "N/M" count, so writing it ref-free reproduces the species "100% but still blue" failure.

This **supersedes** `trait-scan-target-durable-store-2026-06-23.md`'s claim that on-planet
`SetScanned(true)` greens the ref — the in-game fact proves that claim false, and the reason is in
§3 below.

---

## §1 — TRACE OF A REAL HAND-SCAN (what the engine actually writes), with citations

The real scan-input handler is **`ID_90506` @141599f70** `[decompile-verified]` (tc-q3:1-354). When
the player physically completes a scan of the aimed object (whatever `param_1[0xf18]` resolves to),
`ID_90506`:

1. Picks the scan-process SFX from the panel discriminant `*(param_1+0xf2d)`: `'2'`=Plant,
   `'3'`=Animal, **`'5'`=EnvironmentalFeature**, `'6'`=MapMarker (tc-q3:86-110). A trait scan-target
   resolves to **`f2d == 5`** (see §2) → "UIMenuMonocleScannerEnvironmentalFeatureScanProcess".
2. Resolves the aimed ref `plVar11` from `param_1[0xf18]` via the targeting map
   `ID_139363(ID_883285, *(param_1+0xf18), …)` (tc-q3:119-133). **This is the crosshair target, not
   an arbitrary ref handed in from script.**
3. `cVar5 = ID_83007(plVar11)`; only proceeds with the scan write if it returns `1` (= currently
   unscanned) (tc-q3:137).
4. **Transient write + side effects:** `ID_83008(plVar11, 1, 8, uVar12)` (tc-q3:175). `ID_83008`
   (cVar==0 branch, scanned-state.txt:131-143) calls `ID_83038(db, &flag, &refId)` which writes the
   per-ref `939118 +0x28` byte AND, **only if the byte CHANGED**, copies the component's canonical id
   (`+0x24`) to an out-param; that nonzero out-param then triggers `ID_52157` → durable `938333`
   write. (tc-q1:35-47 — `ID_83038` updates `+0x28` and copies `+0x24` *inside* the `if (*newFlag !=
   *oldByte)` block.) `[decompile-verified]`
5. **Durable write (938333):** `ID_52157 → ID_52158` (tc-q2:558-661, 276-554). `ID_52158` keys
   `(938333<<48)|(planetId<<16)` (planetId from `ID_52188`), takes the per-planet sub-DB at
   `slot+0x20`, and:
   - `ID_124898(sub, canonicalId, flag)` → accumulates the scanned byte at the per-canonical record
     `+0x21` (tc-q2:56-127 — writes/【adds to】 `+0x21`). This is the **same `+0x21` the species green
     reads.** `[decompile-verified]`
   - computes a percentage: `bVar13 = ID_47400(canonicalId)` returns the canonical's **form-type
     char**; `'.'`→`ID_69507()`, `'2'`→`ID_69506()`, else denom=1; `pct = round(scanned/denom*100)`
     → `ID_124899(sub, canonicalId, pct)` writes `+0x20` (tc-q2:362-379). `[decompile-verified]`
   - if a threshold is crossed (`oldByte < denom <= newByte` AND target visible) it fires
     `ID_101322(..., ID_909785, 0xd, 1, ref, …)` — the "fully surveyed" engine event (tc-q2:414-416).
6. **Identity reveal + percent re-read for the panel:** `ID_83009(plVar11)` (canonical id) +
   `ID_52188` (planet) + `ID_83041(db, …)` read the durable `938333` state back for display
   (tc-q3:150-190). `local_res8[0]=='d'` etc. drive the "discovered new" toast (tc-q3:237-243).
7. Recomputes the survey aggregate (`ID_64215`/`ID_97851` family) and repaints.

So a real scan writes **two** stores: the transient per-ref `939118 +0x28` (drives outline+count+
identity) and the durable `938333 +0x21/+0x20` (drives survey % + completion events).

---

## §2 — WHAT THE VISIBLE SCAN-TARGET READS FOR count / identity / green

The Monocle aim/target state is set every frame by **`ID_90548` @1415a47c0** `[decompile-verified]`
(tc-branch:628-1218). For the resolved aimed ref `local_1d8`:

- **Outline color** `param_1[0xf2c] = ID_83007(local_1d8)` (tc-branch:771-772). For a scan-target,
  `ID_83007` takes the **`cVar3==0` branch** (base bit-`0x2a` clear) → returns
  `(939118+0x28 != 0) + 1` → **1=blue / 2=green**. (scanned-state.txt:26-68.)
  - **PROVEN by the in-game fact:** Papyrus `IsScanned` = `ID_118370` = `return ID_83007(ref) == 2`
    (tc-q5:1-10). After `SetScanned(true)` set `939118+0x28`, `IsScanned()` returned TRUE ⟹
    `ID_83007` returned 2 ⟹ the scan-target uses the `939118+0x28` transient branch. This
    **resolves the long-open "bit 0x2a authored source" question empirically: bit 0x2a is CLEAR for
    the scan-target ACTI → 939118+0x28 IS its outline source.** `[in-game]`+`[decompile-verified]`

- **Panel discriminant** `param_1[0xf2d]`: `ID_90548` branches on the base form-type char
  `*(local_1d8[0x13]+0x2e)`: `'.'`(FLOR)→flora f2d 2/8, `'$'`→f2d 5/7, **default→f2d 5/9/8**
  (tc-branch:777-879). The scan-target base is an **ACTI** (not `'.'`, not `'$'`) → falls to the
  default branch → **f2d = 5 = "EnvironmentalFeature"** (tc-branch:866-878; confirmed by `ID_90506`
  f2d==5 SFX and `ID_90518` case 5/7/8/9/10 all emitting "UIMenuMonocleScannerEnvironmentalFeature",
  trait-ref-count.txt:1438/1777/1819/1869/1919). `[decompile-verified]`

- **Identity / name reveal** (panel populate `ID_90518` case 5, trait-ref-count.txt:1309-1440):
  the name is `(*(code*)(*local_608)[0xf2])()` (GetName), but the "Unknown vs real name" + harvest
  visibility is gated by **`ID_64337(local_608, ID_922868, 1, 0)`** (trait-ref-count.txt:1398) and
  `ID_44767`. `ID_64337` (tc-q5:14-160) is a complex line-of-sight / known-state gate; for a
  never-properly-scanned feature it returns "unknown". The reveal flips only when the engine's own
  scan/known state is set — which the real scan path establishes and the byte-poke does not drive.
  `[decompile-verified]` for the gate; `[inferred]` that `ID_64337` consults the same per-ref
  scanned/known state.

- **"N/M SCANNED" count.** This is **NOT** the per-ref Monocle panel — `ID_90518` only writes the
  integer count fields `uLocationTraitRefsScanned`(+0xa0)/`uLocationTraitRefsRequired`(+0xc0) inside
  the **fauna-pack** sub-block gated on `*(ref+0xf2e)!=0` (trait-ref-count.txt:1128, writers at
  :1209-1223). The trait "N/M SCANNED" you see on a scan target is a **Location-survey readout**: M =
  the count of `LocRefType PlanetTraitScanTargetLocRef (0x0027A567)` entries in the placed
  `OverlayTrait…Location` LCTN's LCSR (`[esm-verified]`, esm-trait-scan-target-authoring: M=2 for
  trait 20), and N = how many of *those specific loaded LocRef refs* are scanned. The per-LocRef
  "scanned" test the Location count uses is the **same `939118 +0x28`** transient component on each of
  those refs (it is the only per-ref scanned bit the scan-target branch maintains). `[esm-verified]`
  for M; `[inferred]` that N walks the Location's LocRef refs' `939118+0x28`.
  - The global count walker `ID_90522` (tc-branch:34-174) DOES range-scan all `939118` entries
    (`(939118<<48)` key) and feed each `+0x28` byte into `ID_90523` — it is the engine's bulk
    scanned-ref enumerator, again reading `939118+0x28`. `[decompile-verified]`

**Net:** every visible aspect of a scan target (outline, count, identity gate) is downstream of the
per-ref **`939118 +0x28`** transient byte on the **loaded, aimed/Location-member ref instance**.

---

## §3 — WHY PAPYRUS `SetScanned(true)` ON THE FOUND REFS DID NOT COMPLETE IT

Three independent reasons, all `[decompile-verified]`/`[in-game]`:

1. **Wrong instance.** `SetScanned` (`ID_118472`) sets `939118+0x28` on the **`TESObjectREFR` you
   pass it** (tc-q4 native table:2484 → `ID_118472`). The Monocle outline/count/identity read the
   `939118` component of the ref the **aim/Location path** resolves (`ID_90548` `local_1d8` from
   `ID_139363(ID_883285, *(param_1+0xf18))`; the Location count walks the LCTN's own LocRef refs).
   If the mod's `FindAllReferences…` handles are not the exact loaded REFR objects the renderer reads
   (different persistent-handle/instance, or the visible target is a different overlay instance),
   `IsScanned()` on the mod's handle reads back the byte the mod just set (TRUE) while the renderer
   reads a *different* component that is still 0 → BLUE, 0/2, UNKNOWN. The in-game result (IsScanned
   TRUE, visuals unchanged) is the signature of this instance mismatch. `[in-game]`

2. **Side-effects skipped — even on the right instance.** The REAL scan (`ID_90506`) does far more
   than set `+0x28`: it runs the durable `938333` write (`ID_52157→52158→124898/124899`), the
   discovery/identity toast (`ID_83041` re-read + `UIMenuMonocleScannerResourceDiscoverNew`), the
   "fully surveyed" event `ID_101322`, and the survey-aggregate recompute. `SetScanned`
   (`ID_83008`→`ID_83038`) only touches `939118+0x28` and *conditionally* forwards to `ID_52157`
   **only if the byte changed**; the panel identity gate (`ID_64337`) and the count display are not
   poked, so even a correctly-targeted `SetScanned` does not flip "Unknown→name" or move the counter
   in the same frame. `[decompile-verified]` (ID_83008 scanned-state.txt:131-143 vs ID_90506
   tc-q3:111-260).

3. **`ID_52157` is skipped when the byte was already set.** `ID_83038` (tc-q1:35-47) copies the
   canonical id (to trigger the durable `938333` write) **only inside** `if (*newFlag != *oldByte)`.
   A second `SetScanned(true)` after the byte is already 1 → no change → `local_res8[0]` stays 0 →
   `ID_52157` is **not** called → no durable contribution at all. So repeated/idempotent
   `SetScanned` calls also fail to even feed the survey %. `[decompile-verified]`

`IsScanned()` returning TRUE is therefore fully consistent with "nothing visible changed": it only
proves the mod's own handle's `939118+0x28` is now 1, which is exactly the byte `IsScanned` reads and
nothing else the player can see depends on *that instance's* byte.

---

## §4 — IS THERE A REF-FREE / ALL-PLANETS DURABLE STORE?  (verdict + the proof)

**Verdict: NO ref-free durable store completes a trait scan-target.** Proven, not assumed:

- The outline (`ID_83007`→`939118+0x28`), the Location "N/M" count (walks the loaded LocRef refs'
  `939118+0x28`), and the identity gate (`ID_64337` on the loaded ref) all read **per-ref transient
  `939118`** state. `939118` is **created on 3D-attach, deleted on detach, reset to 0 on
  materialization, and has an empty save serializer** — re-verified this pass: `ID_83043` create
  lambda `*(u8*)(puVar8+5)=0` (= component `+0x28`, tc-q4:2813), `ID_83004` likewise stamps `+0x28`
  from a zero (tc-q4:2742-2743), `ID_83029` materialize copies `+0x10/+0x18` from a LIVE source not a
  saved record (tc-q4:2946-2948). So `939118+0x28` cannot exist for an unloaded planet and cannot be
  pre-written from orbit. `[decompile-verified]`

- The ONLY durable, id-keyed, ref-free-writable store a scan touches is `938333` PlayerKnowledge,
  keyed `(planetId, canonicalId)` where the scan-target's canonicalId resolves to its **base ACTI
  formid** (`ID_83006`→`ID_63393` default, since `ID_64338` requires base `+0x2e=='2'` and an ACTI is
  not '2' → tc-q2:255-272, 131-152). Writable ref-free via
  `ID_124898(sub938333_for_planet, baseActiFormId, 0xFF)`. **BUT no scan-target reader consults it:**
  the survey-% aggregator `ID_97851` (tc-q3:745-849) walks `938333 slot+0x38`/`+0x21` for
  flora/fauna/resources and contributes to `fSurveyPercentage`, never to the scan-target outline or
  the "N/M" count. Writing it ref-free gives "survey-% credit but still blue / still 0/2 / still
  Unknown" — the exact species **"100% but blue"** failure mode, now reproduced for scan targets.
  `[decompile-verified]`

- The `937887` trait-known proxy (`ID_52155 SetTraitKnown`) only flips the planet panel "TRAITS N/N"
  knowledge flag; it does not green the surface scan target or move its count. (Unchanged from prior;
  different discriminator.)

So completing a scan target to **green + identified + N==M** is **materialization-bound AND
aim/Location-bound**: it requires the engine's real scan on the loaded ref instance. There is no
off-planet, ref-free, all-planets write that achieves it.

---

## §5 — WHAT (IF ANYTHING) ACTUALLY WORKS ON-PLANET — and the SetScanned fix

If on-planet completion is acceptable (it is the only thing that can green/count/identify), the
correct call is **NOT bare `SetScanned`** but the path that mirrors `ID_90506`. Practically, within
Papyrus/SFSE the closest faithful trigger is to drive the engine's scan on the **exact loaded
LocRef ref instances of the current Location** (the ones the LCTN's LCSR LocRefType-0x0027A567
entries point at), and to also ensure the durable + side-effect path runs. Concretely the fix for
the disproven approach is:

1. Enumerate the **current Location's** scan-target refs (LocRefType `0x0027A567`), i.e. the same
   refs the engine's "N/M" count walks — not a blanket `FindAllReferencesOfType` that may return
   non-rendered/instance-mismatched handles.
2. For each, ensure `939118+0x28` is set **on the instance the renderer reads** AND the durable
   `938333` + identity side-effects fire. Bare `SetScanned` does (1) on the wrong instance and skips
   (2). The engine itself does both inside `ID_90506`; an SFSE plugin can replicate by calling the
   native scan-trigger on the aimed/loaded ref (the `ID_83008(ref,1,8,…)` + `ID_52157` + reveal
   sequence), or by setting `939118+0x28` on the correctly-resolved loaded ref AND separately writing
   `938333 +0x21` for `(planetId, baseActiFormId)` so the survey % also moves.
3. Per-loaded-ref persistence across reload is the engine's normal changed-REFR save of that specific
   loaded ref — it re-greens the SAME ref on re-materialization, but it is NOT an id-keyed knowledge
   record and cannot be pre-written for an unvisited planet.

`[decompile-verified]` for the mechanism; the precise SFSE entry point to invoke `ID_90506`'s
scan-on-aimed-ref is the remaining engineering step (it needs the aimed-ref resolution, not a
script-supplied handle).

---

## ★ STORE / KEY / OFFSET SUMMARY (corrected)

| Visible thing (scan target) | Store | Key | Offset | Durable? | Reader |
|---|---|---|---|---|---|
| Outline green/blue | `939118` ScannableComponent | **loaded REFR FormID** | `+0x28` (byte≠0 → green) | **NO** (empty stub, reset-on-materialize) | `ID_83007` via `ID_90548[0xf2c]`; `IsScanned`=`ID_118370` |
| "N/M SCANNED" count | per-LocRef `939118` of the Location's `0x0027A567` refs | loaded REFR FormIDs | `+0x28` each | NO | Location survey / `ID_90522` walk |
| Identity (Unknown→name) | per-ref scanned/known state on the loaded ref | loaded REFR | (gate `ID_64337`) | NO | `ID_90518` case 5 name + `ID_64337` |
| Survey % credit only | `938333` PlayerKnowledge | (planetId, **base-ACTI canonicalId**) | `+0x21` (scanned), `+0x20` (pct) | **YES** | `ID_97851` aggregator — **NOT** the outline/count/identity |

**Bottom line for the mod:** there is no ref-free durable handle that completes a trait scan target.
The find-refs+`SetScanned` approach failed because `SetScanned` writes `939118+0x28` on a non-rendered
instance and skips the identity/count/durable side-effects of a real scan. The only durable,
ref-free-writable store a scan touches (`938333` by planetId+base-ACTI id) feeds survey % only, so
pre-writing it from orbit yields "% credit but blue/Unknown/0-of-M", never a completed scan target.
