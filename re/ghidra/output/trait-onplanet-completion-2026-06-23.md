# Trait Scan-Target — ON-PLANET native completion (green + N/M + Unknown→named)
### decompile-verified, 2026-06-23

Game: Starfield.exe 1.16.236 (≡ 1.16.244). Ghidra project `ghidra-project/Starfield`.
All IDs are Address Library `ID_<n>` (REL::ID). Confidence tags: `[decompile-verified]`,
`[in-game]`, `[inferred]`.

New headless dumps this pass (`re/ghidra/output/`):
- `onp-resolver-2026-06-23.txt` — **ID_139363** (FormID→REFR resolver), **ID_90518**
  (panel populate), **ID_90522/90523** (count walker), **ID_83038** (`+0x28` writer),
  **ID_83025** (reveal-set helper), **ID_52157/52160** (durable trigger), **ID_124898/124899**
  (durable accumulators), **ID_64338** (canonical-key discriminator).
- `onp-helpers-2026-06-23.txt` — **ID_83019** (survey event sink), **ID_52158** (durable `938333`
  write + `ID_101322` event), **ID_52188** (planetId resolve), **ID_83040/83041** (durable
  read-back), **ID_90507/90530/90513/90517** (`ID_90506`'s SFX/repaint helpers), **ID_64215**,
  **ID_56990**, **ID_83009** (canonical id), **ID_47400/47401** (form-type / FormID→TESForm).

This doc **supersedes** the "materialization-bound AND aim-bound, no native completion" framing of
`trait-true-completion-2026-06-23.md` *for the on-planet, loaded-ref case*: with the right ref and
the full side-effect sequence, **on-planet native completion is ACHIEVABLE** (see §5/§6). The
off-planet / ref-free impossibility from that doc still stands and is unchanged.

---

## ★ ONE-LINE ANSWER

Both the renderer (`ID_90548`) and the real scan handler (`ID_90506`) obtain their scan-target REFR
by the **identical call** `ID_139363(ID_883285, aimFormID, &collector)` — a hash lookup in the global
FormID→REFR handle table `ID_883285`. For one FormID this returns **the one canonical loaded REFR**;
there is **no second instance**. `ID_83007/83008/83009` all key the `939118` ScannableComponent by
`*(REFR+0x28)` (the REFR FormID) into the **same global `939118` registry**, and the "N/M" count
walker `ID_90522` range-scans **that same global `939118` registry** (NOT a Location LocRef list — a
correction to the prior model). Therefore: **if your find-handle's FormID equals the aimed surface
instance's FormID, setting `939118+0x28` on it is read by the renderer and the count.** The prior
on-planet failure was caused by (a) `SetScanned` setting the byte on a ref whose FormID was not the
aimed/loaded surface instance (template / different overlay copy), and/or (b) `SetScanned` skipping
every side-effect a real scan runs: the durable `938333` write, the identity-reveal `ID_83025`, the
"fully surveyed" event `ID_101322`, the survey recompute `ID_97853`, and the panel/outline repaint
`ID_90518`/`ID_90517`. The native recipe in §5 reproduces the full `ID_90506` sequence on the
engine-resolved ref and is decompile-GO; §7 lists the crash guards.

---

## §1 — HOW TO GET THE ENGINE'S ACTUAL SCAN-TARGET REFS (vs the find)

### 1a. The resolver `ID_139363` is a FormID→REFR hash table, not an enumerator `[decompile-verified]`

`ID_139363(table=ID_883285, formID, &collectorLambda)` (onp-resolver:1-31):
```c
uVar3 = formID & *(uint*)(table+0x68);                       // hash mask
plVar4 = (table+0x50 base)[uVar3 * 0x10];                    // bucket slot
lVar2 = *plVar4;                                              // stored REFR* for that FormID
if (lVar2 != 0 && /* FormID high-bits match */)
    (**(code**)(*collector+0x10))(collector, lVar2, formID); // hand REFR to the lambda
```
`ID_883285` is the global **runtime FormID→REFR handle registry**. The collector lambda
(`lambda_3a268ab62c709233b14fc0a10215dde6`) copies the single resolved REFR into a local. So
`ID_139363` resolves a FormID to **exactly one** loaded REFR — the canonical loaded instance.

### 1b. The renderer and the real scan both resolve via this same call `[decompile-verified]`

- Renderer/aim updater `ID_90548` (tc-branch:721): `ID_139363(ID_883285, *param_2, &local_e0)` where
  `*param_2 == *(monocle+0xf18)` (the stored aim FormID) → `local_1d8`. Outline color is then
  `param_1[0xf2c] = ID_83007(local_1d8)` (tc-branch:771-772).
- Real scan `ID_90506` (tc-q3:126-127): `ID_139363(ID_883285, *(param_1+0xf18), &local_100)` →
  `plVar11`; the scan write is `ID_83008(plVar11, 1, 8, uVar12)` (tc-q3:175).
- Cleanup/repaint `ID_90517` (onp-helpers:919): same `ID_139363(ID_883285, *(param_1+0xf18), …)`.

**They are the same REFR object**, because the input FormID (`monocle+0xf18`) and the table
(`ID_883285`) are identical. The "instance" is therefore *defined by FormID identity*.

### 1c. The "N/M" count walks the global 939118 registry — NOT a Location LocRef list `[decompile-verified]` (CORRECTION)

`ID_90521`(entry) → `ID_90522`(walker) (onp-resolver:2030-2170):
```c
local_res18 = (u64)ID_939118 << 0x30;                        // 939118-domain key prefix
puVar6 = ID_126805(*param_2 + 0x268, …, &local_res18);       // RANGE query over the 939118 hashmap
do {
    plVar7 = ID_47401(*(u32*)(entry+0x20));                  // entry FormID -> TESForm
    lVar8  = (*(plVar7+0x228))();                            // -> the live REFR
    … distance / line-of-sight filter vs player (ID_63394/ID_62297) …
    ID_90523(*param_1, lVar8, *(u8*)(entry+0x28));           // feed the +0x28 byte into the tally
} while (next entry);
```
`*param_2 + 0x268` is the **ScannableComponent manager's** BSTHashMap of all live `939118` entries
(the manager handle is `*(ID_126578()+0x8b0)`, the same one `ID_83007/83008/83009` use).
`ID_90523` then classifies each loaded scannable in range (state 0..12) and updates the panel's
N/M and per-target icon. **So count and outline read the same `939118+0x28` store, keyed by FormID;
there is no separate LocRef-instance the count consults.** (The LocRefType `0x0027A567` in the ESM is
how the engine *places* M scan targets at authoring time; at runtime the count is "how many loaded
scannables in range have `+0x28 != 0`", which converges to the same M once all M are loaded.)

### 1d. Native way to obtain the engine's refs

Two equivalent native options, both yield the FormID-canonical loaded REFR the renderer/count use:

1. **Resolve by the aimed FormID** (most faithful): read the monocle/scanner menu object's aim slot
   `*(monocleMenu + 0xf18)` (a `u32` FormID) and call
   `ID_139363(ID_883285, aimFormID, &collector)` — or simply `TESForm::LookupByID(aimFormID)` then
   QI to `TESObjectREFR*`. This is the **exact** ref `ID_90548`/`ID_90506` use. Reaching the monocle
   menu object from SFSE is the one extra hop (UI menu registry); if that is awkward, use option 2.
2. **Use `FindAllReferencesWithKeyword(0x001CBEA3, r)`** (find-references-api doc: WILL-FIND, returns
   the loaded procedural surface instances) **but key by FormID**: for each returned REFR, its
   `ref->GetFormID()` is the same FormID the renderer would resolve **iff** it is the aimed surface
   instance. The find returns *all* loaded matches in radius, so you complete *all* of them, which is
   what "N/M → M/M" requires anyway. The prior failure means at least one returned handle was NOT the
   rendered/aimed FormID (an un-rendered template copy or a different overlay instance); guard by
   only acting on refs that pass `ID_83007(ref) != 0` (component exists ⇒ it is a live scannable the
   renderer can read — see §7).

**Pin-down of the discrepancy:** it is **FormID identity**, not "two live objects for one FormID."
`ID_883285` cannot hold two REFRs for one FormID. So a `SetScanned` that visibly did nothing set
`939118+0x28` on a REFR whose **FormID differs** from the aimed/rendered surface instance (a
template/duplicate the find also returned), or it correctly hit the FormID but the panel never
repainted because the side-effects/repaint (§2,§5) never ran. Both are fixed by §5.

---

## §2 — THE MINIMAL COMPLETION SEQUENCE PER REF (what a real scan writes)

Real handler `ID_90506` on the resolved ref `plVar11` (tc-q3), gated by
`ID_83007(plVar11)==1` (state 1 = component exists, unscanned):

| Step | Call (in `ID_90506`) | Writes / effect | Needed for VISUAL? |
|---|---|---|---|
| A | `ID_83008(plVar11, 1, 8, uVar12)` | sets `939118+0x28 = 1` via `ID_83038`; **if byte changed**, forwards canonical id to `ID_52157` | **YES — green + count** |
| B | `ID_52157 → ID_52158` | durable `938333` write (`+0x21` scanned, `+0x20` pct via `ID_124898/124899`); fires `ID_101322` "fully surveyed" event when threshold crossed; `ID_83025` reveal-set | name/event/% (see below) |
| C | `ID_52157 → ID_97853` | survey-% recompute + `ID_64213/64214` survey events | survey % only |
| D | `ID_83025(panelDB, plVar11, canonId)` (also reached via `ID_90506` `local_res8[0]=='d'`) | marks the canonical id "discovered/known" in the `ID_937609+0x160` known-set | **YES — Unknown→named** |
| E | `ID_90513` + `ID_90530` + `ID_90517` | scan-complete SFX, monocle re-evaluate, **`ID_90517`→`ID_90548` repaint** (re-reads `ID_83007`, re-populates `ID_90518`) | **YES — forces the panel/outline to refresh same frame** |
| F | `ID_90507(param_1,1)` | early survey-credit path (only when `*(ID_939103+0xf20)+0x28 != 0`) | survey % only |

Mechanism details `[decompile-verified]`:
- **`ID_83038`** (onp-resolver:2624) writes `+0x28` and, **only inside `if (*newByte != *oldByte)`**,
  copies the component canonical id (`+0x24`) to the out-param → that nonzero out-param is what makes
  `ID_83008` call `ID_52157`. So an **idempotent re-set does nothing** (no durable, no event). To
  drive B/C you must transition the byte 0→1, or call `ID_52157` yourself.
- **`ID_52158`** (onp-helpers:47) is the durable writer: `ID_124898(rec, canonId, scannedByte)` →
  `+0x21`; computes pct via `ID_47400`(form-type) → `'.'`=`ID_69507()`, `'2'`=`ID_69506()`, else 1 →
  `ID_124899` → `+0x20`. At lines 185-187 it fires
  `ID_101322(ID_922868, ID_909785, 0xd, 1, ref, 0, 0)` **iff `oldByte < threshold <= newByte` AND
  the target is known/visible (`bVar8`)** — i.e. the "fully surveyed" event is a *threshold-crossing*
  side-effect of the durable write, not an independent call.
- **Identity reveal** (`Unknown → named`) is gated in the panel by `ID_64337` (tc-q3:856) +
  `ID_44767`, which consult the **known-set** at `ID_937609+0x160` that `ID_83025` writes (step D)
  and the durable `938333` "discovered" flag re-read by `ID_83040/83041`. `ID_90506` runs `ID_83025`
  whenever the durable re-read returns `local_res8[0]=='d'` (newly discovered).

**Strictly-required-for-VISUAL subset (green + count + name), minimal:**
- Green outline + N/M count: **A** (set `939118+0x28`) on the FormID-correct loaded ref **+ E**
  (repaint). That alone greens it and moves the count, because both read `939118+0x28`.
- Unknown→named: **D** (`ID_83025` into the `ID_937609+0x160` known-set for the ref's canonical id)
  **+** the panel repaint **E**. Without D the panel's `ID_64337` gate keeps it "Unknown".
- "Fully surveyed" toast/event + survey %: **B + C** (the `ID_52157` chain). Not required for the
  three visuals, but required to match a real scan (and to make the planet survey % move).

---

## §3 — WHY THE PRIOR `SetScanned` ATTEMPT FAILED (now fully explained)

Papyrus `SetScanned` (`ID_118472`, setscanned.txt:1-20) `[decompile-verified]`:
```c
cVar1 = ID_83007(param_3);                 // component state
if (cVar1 != '\0') { ID_83008(param_3, param_4, 0xd, 0); return; }   // LIVE component path
… else ID_83006/ID_83005 (durable-only path) …
```
For a loaded scan-target with a live `939118` component, it takes the first branch and calls
`ID_83008(ref, value, 0xd, 0)` — **the same `+0x28` write as a real scan** (real scan uses mode `8`;
the mode byte only tags the survey-event "reason" passed to `ID_52157`, the `+0x28` write is
identical). So the write mechanism was correct. It failed because:

1. **FormID/instance**: `SetScanned` set `+0x28` on whatever REFR the mod passed. If that handle's
   FormID ≠ the aimed/rendered surface instance's FormID (a template/overlay duplicate the find also
   returned), the renderer's `ID_83007(local_1d8)` reads a **different** `939118` entry still 0 →
   BLUE / 0-of-M / Unknown, while `IsScanned()` on the mod's handle reads back its own 1 → TRUE.
   (The single-FormID guarantee of `ID_883285` means this is a *FormID-mismatch*, not two live copies
   of one FormID.) `[in-game]` + `[decompile-verified]`
2. **No side-effects + no repaint**: even on the right FormID, `SetScanned` runs **only** step A. It
   does not run D (identity), B (event/durable), or E (repaint). The panel does not re-evaluate
   `ID_64337`/`ID_90518` that frame, so Unknown stays Unknown and the count text isn't redrawn until
   the engine next repaints the monocle — and a second idempotent `SetScanned` then no-ops `ID_83038`
   so it never even feeds the durable %. `[decompile-verified]`

---

## §4 — INSTANCE-RESOLUTION VERDICT

- **Is it a different REFR for the same object, or multiple instances?** It is **FormID identity**.
  `ID_883285` maps one FormID → one loaded REFR. The renderer, the count walker, the scan handler,
  `IsScanned`, and `SetScanned` all key the `939118` component by `*(REFR+0x28)` = REFR FormID. So
  "the right ref" = the loaded surface REFR **whose FormID is the one the monocle has aimed**
  (`*(monocle+0xf18)`), which is also the one the global `939118` count walker enumerates.
- The find (`FindAllReferencesWithKeyword 0x001CBEA3`) returns **all** loaded matches by base keyword;
  some may be non-rendered template/duplicate FormIDs. Filtering returned refs by
  `ID_83007(ref) != 0` keeps only those with a live `939118` component (the ones the renderer/count
  can actually read). Completing **all** of those is exactly what drives N/M → M/M.

---

## §5 — NATIVE-CALLABLE RECIPE (per loaded scan-target REFR), decompile-GO

Given a loaded scan-target `REFR* ref` (from §1d) and `ID_83007(ref) != 0` (component exists):

**Faithful full completion (mirror `ID_90506`):**
```
1.  if (ID_83007(ref) == 0) skip;                 // no live component — do NOT proceed (guard, §7)
2.  ID_83008(ref, /*scanned*/1, /*mode*/8, /*credit*/0);
        // sets 939118+0x28=1 via ID_83038; because byte 0->1 changed, it auto-calls
        // ID_52157(ref, canonId, 8) -> ID_52158 (durable 938333 +0x21/+0x20, ID_101322 event)
        //                            -> ID_97853 (survey recompute).  Steps A,B,C in one call.
3.  // identity reveal (step D): ensure the canonical id is in the known-set.
    // ID_90506 reaches this via the durable re-read returning 'd'; to force it natively:
    canonId = ID_83009(ref);
    panelDB = *(longlong*)(ID_937609 + 0x160);
    ID_83025(panelDB, ref, canonId);              // mark discovered/known
4.  // repaint (step E): re-run the monocle aim/panel update so green+count+name redraw now.
    //   either call the monocle menu's update (the menu object that owns +0xf18), or
    //   rely on the next natural ID_90548 tick. ID_90517(monocle) -> ID_90548 if menu reachable.
```
**Minimal green+count only** (if you accept name/% lagging to next scan): just step 1+2 on every
FormID-correct loaded ref, then let the monocle repaint (look away/back). `ID_83008(ref,1,8,0)` is
the single load-bearing call for the outline and the count.

**Notes on the calls**
- `ID_83008(ref, 1, 8, 0)` is the safe, complete one-call path: it sets the byte AND fans out to the
  durable + event + recompute via `ID_52157` (because the byte changes 0→1). Prefer this over poking
  `+0x28` directly so the `if(changed)` durable forward fires.
- `ID_52157(ref, ID_83009(ref), 8)` may be called directly if you ever set the byte some other way
  and need the durable/event without a byte transition (e.g. re-completing). It internally re-derives
  planetId via `ID_52188` and is ref-keyed-safe.
- `ID_83025(*(ID_937609+0x160), ref, ID_83009(ref))` is the identity-reveal write; it is the same
  call `ID_90506` and `ID_90518` use for the known-set.
- All of these are `__fastcall` engine functions taking a `TESObjectREFR*` first arg — directly
  callable from an SFSE plugin via REL::ID once you have the loaded ref.

---

## §6 — VERDICT

**ON-PLANET native completion of a trait scan-target is ACHIEVABLE.** `[decompile-verified]`

The green outline, the N/M count, and the Unknown→named identity all read **per-ref `939118+0x28`**
plus the **known-set at `ID_937609+0x160`**, both keyed by the REFR FormID and both writable by the
engine's own functions on a *loaded* ref:
- green + count: `ID_83008(ref, 1, 8, 0)` on the FormID-correct loaded REFR;
- identity: `ID_83025(*(ID_937609+0x160), ref, ID_83009(ref))`;
- durable %/event: ride along automatically inside `ID_83008`→`ID_52157`→`ID_52158/97853`;
- visible refresh: monocle repaint (`ID_90548`, naturally on next tick or forced via `ID_90517`).

The earlier "not natively completable" conclusion applied to the **off-planet / ref-free / all-planets
pre-write** case, which remains **NOT achievable** (the `939118` component is created on 3D-attach,
zeroed on materialize, has an empty save serializer — `trait-true-completion-2026-06-23.md` §4, still
valid). On-planet, with the loaded ref in hand, the sequence above is the real scan minus the
hand-scanner animation. **It is NOT input-only / not hand-scanner-bound** — every state write the
scan performs is an ordinary engine function call on the ref.

---

## §7 — CRASH GUARDS (why prior `ID_83024` / `ScanNearbyRefs` faulted)

1. **Component-existence guard (mandatory).** Call `ID_83008`/`ID_83009`/`ID_83025` **only if
   `ID_83007(ref) != 0`**. `ID_83007` returns 0 when the `939118` ScannableComponent does not exist
   for the ref (no 3D / not a live scannable). A `0` return means there is no `+0x28` to write and no
   canonical id to read — proceeding dereferences a missing component. The prior `ID_83024` ref-free
   call FAULTED precisely because it ran the materializer/known-set write without a live component.
   `ID_83024` is the **internal** known-set writer reached *inside* `ID_83025` only after a valid
   `ID_56887` slot lookup (onp-resolver:2729) — never call `ID_83024` directly; call `ID_83025`.
2. **Form-type guard.** Only act on refs whose base is the scan-target ACTI (`0x0021B250`-class) or
   that carry `Handscanner_AllowScanAtHighlightRange` `0x001CBEA3`. `ID_90522`/`ID_90523` and
   `ID_64337` branch on the base form-type char `*(ref[0x13]+0x2e)`; feeding a wrong-type ref into the
   panel/known-set paths is what crashed `ScanNearbyRefs` on procgen cells (refs with null/odd base).
3. **Loaded/attached guard.** Require `*(ref+0x98) != 0` (cell-attached / has 3D) — the same gate
   `ID_63054`/`ForEachReference` enforces. Unloaded template FormIDs have no `939118` and no valid
   `+0x98`; `FindAllReferencesWithKeyword` already excludes them, but validate before native writes.
4. **Player/planet context guard.** `ID_52157`→`ID_52158` resolves planetId via `ID_52188`, which
   reads the player's current location DB (`ID_37878(player+200, 0x81)`, `ID_937889`). Only call on
   the surface (player in the overlay area) so `ID_52188` returns a valid planetId; calling in a
   context with no planet record can leave `*param_2/param_3 = 0` and write a degenerate durable key.
   `ID_83008` itself is safe (it short-circuits on `ID_36022(...,0x2a)` and on the byte not changing);
   it is the durable `ID_52157` fan-out that needs a valid planet context.
5. **Idempotency.** A second `ID_83008(ref,1,…)` after the byte is already 1 no-ops the durable
   forward (`ID_83038`'s `if(changed)`), so it is safe to re-run but will not re-fire the event.
6. **Don't hand-write `+0x28`.** Prefer `ID_83008` over a raw byte poke so the `if(changed)` durable
   forward and the manager refcount bookkeeping run correctly.

---

## ★ STORE / KEY / OFFSET SUMMARY (on-planet, corrected)

| Visible thing | Store | Key | Offset | Native write (loaded ref) | Reader |
|---|---|---|---|---|---|
| Green outline | `939118` ScannableComponent | **REFR FormID** (`ref+0x28`) | `+0x28` (≠0 ⇒ green) | `ID_83008(ref,1,8,0)` | `ID_83007` via `ID_90548[0xf2c]`; `IsScanned`=`ID_118370` |
| "N/M" count | global `939118` registry (range-walked) | REFR FormID per entry | `+0x28` each | same `ID_83008` per loaded ref in range | `ID_90522`→`ID_90523` |
| Unknown→named | known-set | canonical id (`ID_83009(ref)`) | `ID_937609+0x160` slot | `ID_83025(*(ID_937609+0x160), ref, ID_83009(ref))` | `ID_90518` name + `ID_64337` gate |
| "Fully surveyed" event | (transient) | ref | — | rides `ID_83008`→`ID_52157`→`ID_52158` (threshold) | `ID_101322(…ID_909785,0xd,1,ref…)` |
| Survey % credit | `938333` PlayerKnowledge | (planetId, base-ACTI canonId) | `+0x21`/`+0x20` | rides `ID_83008`→`ID_52157`→`ID_52158`/`97853` | `ID_97851` aggregator |

**Bottom line:** ACHIEVABLE on-planet. Resolve the loaded scan-target REFR by its aimed FormID via
`ID_139363(ID_883285, …)` (or filter the keyword-find by `ID_83007(ref)!=0`), then per ref run
`ID_83008(ref,1,8,0)` (green + count + durable/event), `ID_83025(*(ID_937609+0x160),ref,ID_83009(ref))`
(name), and let the monocle repaint. Guard with `ID_83007(ref)!=0`, correct base form-type,
cell-attached, and on-surface planet context.
