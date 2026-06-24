# Trait Scanner Panel "N/M SCANNED" — TRUE source + contradiction reconciled (2026-06-23)

Resolves the live in-game contradiction: the mod set the per-ref `939118 +0x28` byte to 1 on
BOTH in-range Microbial-Community targets (log: `rawByte=1`, `ID_83007` returns 2), the objects went
BLUE and reveal their description, **yet the panel still reads "0/2 SCANNED" / "UNKNOWN FEATURE".**

All claims `[decompile-verified]` against `ghidra-project/Starfield` (1.16.236≡244).
Headless dumps produced this session live in `re/ghidra/output/`:
`xrefs-90486-90506-2026-06-23.txt`, `model-vtable-2026-06-23.txt`, `model-populate-90624/90541`,
`tally-90502-90548/tally-builders`, `xrefs-938422-2026-06-23.txt`, `g938422-writers-2026-06-23.txt`,
`refresh-fns-2026-06-23.txt`, plus prior `trait-ref-count.txt`, `tc-q2/q3`, the `bejwvvrvo` ID_90518 C dump.

---

## ★ ONE-LINE ANSWER

The panel "N" (`uLocationTraitRefsScanned`, model+0xa0) is written by **ID_90518** as
`model+0xa0 = (uint)*(u8*)(ID_938422 + 0x38)` — a tally byte on the **scanner-menu runtime state
object `ID_938422` (global @ `0x1461ea0c8`)** that is (re)built ONLY by the engine's own scan /
re-aim refresh pass (`ID_90530`/`ID_90517→ID_90548`/`ID_90507`, run by the real scan handler
`ID_90506`). It is NOT a per-frame recount of the transient `939118 +0x28` bytes the mod pokes.
Our `SetScanned` writes the `939118 +0x28` byte (what colors the outline BLUE and satisfies
`ID_83007`/`IsScanned`), but it never runs the scan-complete refresh that recomputes
`ID_938422 +0x38`, and it never writes the durable `938333` store the title-reveal / survey count
read. **So N stays 0 and the title stays "UNKNOWN FEATURE."**

---

## §1 — The "N/M" binding (who the panel reads)

`ID_90486 @141597d40` is the `MonocleUIDataModel` GFx serializer. It binds Scaleform vars to model
struct offsets `[decompile-verified]` (`trait-ref-count.txt:9-10`):

```
model+0x08  bShowPlanetInfo
model+0x28  aPlanetTraits
model+0xa0  uLocationTraitRefsScanned   <-- the N in "N/M SCANNED"   (TUIValue<uint>, value@+0x18 of the wrapper)
model+0xc0  uLocationTraitRefsRequired  <-- the M
model+0x260 fSurveyPercentage  ...  (uResources/uFlora/uFauna Current/Max, etc.)
```

Each field is a `TUIValue<T>` (vtable + value + dirty-flag; constructed in `ID_90541`
`MonocleUIDataModel::SocialSpellData` — confirms the wrapper layout). The "set value + notify"
pattern (`if (*(uint*)(slot+0x18) != v){ *(uint*)(slot+0x18)=v; vfunc+0x48(slot,1); }`) is the only
way these change.

## §2 — Who WRITES model+0xa0 = N  `[decompile-verified]`

The per-target Monocle painter **`ID_90518 @14159b890`** (dispatched via `ID_90676/90661→ID_90518`)
writes N. From the raw asm (`bejwvvrvo.txt` C / `b4jhdbrfo.txt` asm):

```
14159ba17  MOV  RDI,[0x1461ea0c8]          ; RDI = ID_938422 (scanner-menu state object)
14159ba1e  LEA  RBX,[RDI+0x50]; CALL lock  ; ID_123862 lock on ID_938422+0x50
14159ba2a  VMOVUPS [RBP+0x1a8] <- [RDI+0x10] (32 bytes)   ; snapshot ID_938422+0x10..0x30
...
14159ba7c  MOVZX EDI, byte ptr [RBP+0x1d0] ;  RBP+0x1d0 == (RBP+0x1a8)+0x28 == ID_938422+0x38
14159baad  CMP  [RBX+0x18],EDI ; MOV [RBX+0x18],EDI ; notify   ; RBX=model+0x88 → value@model+0xa0
```

Decompiled (`trait-ref-count.txt:271-302`):
```c
lVar9 = ID_938422;                       // = *(0x1461ea0c8)
ID_123862(ID_938422 + 0x50);             // acquire
local_360 = *(undefined1[16]*)(lVar9 + 0x30);
bVar13   = local_360[8];                 // == *(u8*)(ID_938422 + 0x38)   <-- THE N VALUE
... (release ID_938422+0x54) ...
if (*(uint*)(model + 0xa0) != (uint)bVar13) {
    *(uint*)(model + 0xa0) = (uint)bVar13;     // N = uLocationTraitRefsScanned
    notify(model+0x88);
}
```

**N = `(uint)*(u8*)(ID_938422 + 0x38)`.** There is **no** write to `model+0xc0` (M) anywhere in
ID_90518 (verified: zero dword stores to `[RSI+0xc0]`); M is sourced from the same `ID_938422`
tally object (its companion required-count field) and is likewise refreshed by the engine pass, not
recomputed from 939118.

`ID_938422` is the scanner/Monocle runtime state global (a large object: +0x10 region = a small
cached array updated by `ID_102619`/`ID_102624`; +0x200 region = camera; `xrefs-938422` shows it is
WRITTEN as a pointer by `ID_82618`(=0), `ID_102619`, `ID_102624` — i.e. (re)bound on location/scan
state change — and READ by the whole Monocle module incl. `ID_90518/90502/90548`). The `+0x38` tally
byte is produced by the scanner's own scan/refresh state machine, **not** by walking 939118 each
frame.

## §3 — What the mod's SetScanned does vs. what a real scan does

**Real scan = `ID_90506 @141599f70`** (`tc-q3:1-354`). For the aimed ref `plVar11` (from
`param_1+0xf18`, the crosshair target):
1. `cVar5 = ID_83007(plVar11)` gate — `ID_83007` reads the **transient 939118 +0x28** byte
   (`refresh-fns:41-50`: key `(939118<<48)|(refFormID<<16)`, returns `(byte!=0)+1`). Our mod's
   `SetScanned(true)` makes this return 2 — confirmed by the log (`state=2`).
2. `ID_83008(plVar11,1,8,..)` writes the **939118 +0x28** byte = our mod's only effect.
3. `ID_52157→ID_52158` writes the **durable 938333** store (`tc-q2:276-379`): key
   `(938333<<48)|(planetId<<16)` (`planetId=ID_52188`), and via `ID_124898` accumulates the scanned
   byte at the per-canonical record **+0x21**, via `ID_124899` writes the % at +0x20. **This is the
   store the title-reveal and the survey/% aggregator read.** Our mod never writes it.
4. Refresh side-effects our mod never runs:
   - `ID_90513(param_1)` — scan-complete toast + UI command
   - `ID_90530(param_1)` — rebuilds scanner state (`ID_936593`, `ID_937609+0x200`, survey aggregate
     via `ID_1016657`); recomputes the camera/tally inputs
   - `ID_90517(param_1, +0xf18)` → **`ID_90548()`** — re-selects/re-aims the scan target and
     reschedules the `ID_90676→ID_90518` paint, i.e. the pass that recomputes `ID_938422+0x38`→N
   - `ID_90507(param_1,1)` — additional UI command

So a real scan: sets 939118 byte → writes durable 938333 (+0x21/+0x20) → fires the refresh that
rebuilds `ID_938422+0x38` → ID_90518 copies it to model+0xa0 → "1/2 SCANNED". The mod stops at the
first step.

## §4 — WHY +0x28=1 on both targets still yields N=0  (the reconciliation)

- N (`model+0xa0`) does NOT read `939118 +0x28`. It reads `ID_938422 +0x38`, a tally the engine
  recomputes only on its scan/refresh pass. The mod set the byte but never triggered that pass, so
  `ID_938422+0x38` is still its stale value (0) → N=0. `[decompile-verified §2,§3]`
- The BLUE outline DID update because the per-frame outline pass (`ID_90521→ID_90522→ID_90523`)
  reads the `939118 +0x28` byte directly (`onp-resolver:2121` passes `*(u8*)(entry+0x28)` as
  `ID_90523 param_3`, which sets the outline state code `local_1b0`), and the INFO/description
  updates from the same per-frame per-target read. Those paths have a SEPARATE refresh from the N/M
  count — exactly the asymmetry observed.
- The "UNKNOWN FEATURE" title is gated on the durable-store reveal, not the byte: case-5 of ID_90518
  (`bejwvvrvo.txt:1310-1441`) reads durable 938333 via `ID_124900(... , ID_83009(ref) , local_538)`
  keyed `(938333<<16,planetId)` and only resolves a name when that record says discovered; with no
  938333 write it stays the empty/`$ScanMapMarker_Unscanned` placeholder.

This is the same shape as the species "100% but still blue / count disjoint" result: the per-ref
color byte and the durable-store count are two different domains
(`species-scan-complete-model-2026-06-23.md:35-43`).

## §5 — THE FIX (callable on a loaded ref from the SFSE plugin)

There is no single byte to poke for the count. To move "N" (and reveal the title), reproduce the
real-scan side effects, in order, on the aimed/loaded ref:

1. **Durable 938333 write (drives title-reveal AND the survey/% count):** call **`ID_52157`** —
   `ID_52157(player /*ID_922868*/, ref->FormID /*= *(u32*)(ref+0x28)*/, 7)` (`tc-q2:558` /
   `ID_90506:114`). This runs `ID_52158` → `ID_124898`(+0x21)/`ID_124899`(+0x20) keyed by
   `planetId=ID_52188(ref)` + `canonical=ID_83009(ref)`. (Address-lib IDs: `ID_52157 @1407b7fa0`,
   needs the player handle + the ref's u32 FormID + the constant `7`.)
2. **Trigger the count refresh** so `ID_938422+0x38` is rebuilt and ID_90518 repaints N/M. The real
   handler does this via `ID_90530` + `ID_90517`(→`ID_90548`). The clean, in-engine way is to drive
   the whole thing through the real handler instead of hand-poking: see option B.

**Option A (piecemeal natives, риск of partial state):** after `ID_83008(ref,1,8,..)` (already
wired), also call `ID_52157(player, ref+0x28 FormID, 7)`, then force a Monocle refresh by invoking
`ID_90517`/`ID_90530` on the active `MonocleMenu` (param_1 = the menu, `param_1+0xf18` = aimed ref
handle). Requires obtaining the live MonocleMenu pointer — fragile from script.

**Option B (recommended — let the engine do it):** the only robust trigger is the engine's own scan
input on the loaded, aimed ref. The count/title machinery is **aim+refresh-bound**: it keys off
`param_1+0xf18` (the crosshair target on the open MonocleMenu) and the refresh tasks posted by
`ID_90506`. A ref-free / not-currently-aimed write cannot rebuild `ID_938422+0x38`. So the
honest, durable result for the COUNT is: drive `ID_52157` for the durable store (gets the title
reveal + survey-% credit, persists across saves) and accept that the literal "N/M SCANNED" tally
only re-renders when the scanner refresh runs (i.e. when that target is actually scanned/aimed in
the Monocle). This matches the established "trait scan-target completion is materialization+aim
bound; only on-planet engine scan input greens/counts it" conclusion
(`trait-true-completion-2026-06-23.md` §3, `trait-scan-target-model` OPEN note).

### Exact symbols / signatures for the plugin
- `ID_83007 @1413076d0`  `char IsScannedState(REFR* ref)` → 1 unscanned / 2 scanned (reads 939118+0x28).
- `ID_83008 @ (scanned-state)` — sets 939118+0x28 (already wired as our SetScanned).
- `ID_52157 @1407b7fa0`  `void CreditScan(Actor* player=ID_922868, uint32 refFormID, int kind=7)`
  → durable 938333 (+0x21 scanned, +0x20 pct) keyed planetId(`ID_52188`)×canonical(`ID_83009`).
- `ID_52188 @ ...`  `bool GetPlanetId(REFR*, uint32* outPlanet, uint32* out2)`.
- `ID_83009 @ ...`  `uint32 GetCanonicalId(REFR*)`.
- `ID_90506 @141599f70`  the full scan handler (does 1-4 of §3); `ID_90517/90530/90513/90507` the
  refresh tasks; `ID_90518 @14159b890` the painter that copies `ID_938422+0x38`→model+0xa0.
- `ID_938422` = global pointer @ **`0x1461ea0c8`** (scanner-menu state; `+0x38` byte = N source).

## §6 — Does this also affect "Unknown FEATURE → named" title?
Yes — same gate. Case-5 of ID_90518 reveals the feature name only from the **durable 938333** record
(`ID_124900` keyed planet×canonical). With no 938333 write the title is the empty placeholder
(`&ID_392439` / `$ScanMapMarker_Unscanned`). It is NOT directly gated on "N hitting M"; it is gated
on the durable per-canonical "discovered" state, which `ID_52157` writes. So the §5 step-1
`ID_52157` call is what flips BOTH the title to the named feature AND credits the survey count;
the literal on-screen "N/M" digits additionally need the scanner refresh (§5 option B) to re-render.
