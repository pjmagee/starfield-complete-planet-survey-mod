# Trait scan-target durable store — DECODED `938333` record (three-save diff, 2026-06-23, CORRECTED)

---

## ★★★★★★★★★ NINTH (2026-06-24 PM — the CLOC-writer + count-source hunt, with a VERIFIED Frida hook table): the `ID_90506` scan fan-out was decompiled exhaustively; **NO synchronous call in it writes a BGSLocation ref list** — the CLOC change-form is produced OUTSIDE the survey-DB fan-out (deferred / Location-discovery domain). The on-screen "N" digit is **`*(u8*)(ID_938422+0x38)`** (a scanner-menu tally), NOT the `939118+0x28` byte. Hook table below is decompile-verified cold-vs-per-frame.

> **Method.** Full headless decompiles this pass (`re/ghidra/output/`): `tc-q3-2026-06-23.txt`
> (`ID_90506` complete, `ID_90521`), `onp-resolver-2026-06-23.txt` (`ID_90518`, `ID_90522`,
> `ID_90523`, `ID_83038`, `ID_83025`, `ID_52157`), `onp-helpers-2026-06-23.txt` (`ID_52158`,
> `ID_83019`, `ID_90507/90513/90517/90530`, `ID_56990`, `ID_83041`), `loc-helpers-2026-06-24.txt`
> (`ID_101322`, `ID_37878`, `ID_44870`, `ID_62706`, `ID_63417`), `loc-reflist-2026-06-24.txt`
> (`ID_57192/57010/63054`), `hookfreq-xrefs-2026-06-24.txt` (caller xrefs → cold/per-frame),
> `xrefs-90486-90506-2026-06-23.txt`. Save anchor: `verify_render_gate.py` + `/tmp/probe_cloc2.py`
> (CLOC change-form header decode).

### A. THE CLOC WRITER — NOT in the scan-DB fan-out  `[decompile-verified, NEGATIVE result]`

The real hand-scan handler **`ID_90506 @141599f70`** (file-offset **`0x1599f70`**) was decompiled in
full (`tc-q3:1-354`). On the aimed/resolved ref `plVar11` (from `ID_139363(ID_883285, *(monocle+0xf18))`)
its ENTIRE side-effect set is:

| line | call | what it writes | touches a Location ref list? |
|---|---|---|---|
| 175 | `ID_83008(plVar11,1,8,uVar12)` | transient `939118+0x28`; fans to `ID_52157` **iff byte 0→1** | NO |
| (83008→) | `ID_52157→ID_52158` | durable **938333** `+0x21/+0x20` + pooled member array; `ID_101322` event; `ID_83025` known-set | NO |
| 114 | `ID_52157(player, *(loc+0x28), 7)` (early credit, when `*(ID_939103+0xf20)+0x28!=0`) | 938333 only | NO |
| 164-173, 195-232 | `ID_83025(*(937609+0x160), ref, canon)` | **known-set** marker array at `biome+0x20` (transient) | NO |
| 262 | `lVar8 = ID_37878(*(player+200), 0x81)` | **READS** the player's ExtraLocation (the CLOC Location) — to get the planet count, NOT to write | NO (read-only) |
| 272 | `ID_56990(local_120, &local_78, player, 0)` | **READS** ExtraLocation `+0x28` count / resolves planetId | NO (read-only) |
| 278-282 | `ID_1016657` survey aggregator into `ID_936593` | transient per-frame survey cache | NO |
| 324-342 | `ID_64215()` + `ID_123824` | posts a `PlayerPlanetSurveyProgressEvent` sink | NO |
| 349 | `ID_90517(param_1, +0xf18)` → `ID_90548` + repaint task | re-aim + schedule `ID_90518` paint | NO |

I decompiled every callee that could plausibly touch a Location (`ID_52158`, `ID_83019`, `ID_90530`,
`ID_90507`, `ID_90513`, `ID_90517`, `ID_56990`, `ID_101322`) — **none appends a reference into a
BGSLocation ref/LocRef list.** `ID_52158` (`onp-helpers:47`) writes ONLY: `db+0x268` 938333
(`ID_124898/124899`), the `ID_101322` threshold event, and the `biome+0x20` known-set marker array
(`ID_56887`/`ID_35755`). `ID_56990`/`ID_90530` both call `ID_37878(player+200, 0x81)` purely to READ
the ExtraLocation's `+0x28` planet count and `+0x18/+0x20` stored position — they resolve the planetId,
they do not write the Location.

**Conclusion:** the `CLOC` change-form that `verify_render_gate.py` proves is the durable difference is
**produced by a path OUTSIDE the survey/scan-DB fan-out** — the generic Creation-Engine
**BGSLocation reference-discovery / LocRefType-resolution** machinery (the `ID_571xx` LocationManager
family that owns the `db+0x268` component family **`ID_938083`**, keyed by ref FormID), which fires when
the player first *encounters/acquires* a placed LocRef'd reference, and/or a deferred post-scan task —
NOT a call the scan handler makes synchronously on the aimed ref. There is therefore **no single
`ID_905xx`/`ID_521xx`/`ID_830xx` "append-to-Location" function to mirror.** This is consistent with the
EIGHTH section's verdict (CLOC is a per-Location change-form, not a survey-DB record) and SHARPENS it:
the survey natives the mod already calls are provably exhaustive for the survey-DB domain; the missing
durable bit lives in the Location-discovery domain, which is reference-/materialization-bound.

### B. The CLOC change-form is a BGSLocation sub-record  `[save-verified]`

`/tmp/probe_cloc2.py` decoded the change-form header. The save has **43** `CLOC` sub-records (one per
visited Location with runtime changes); each is framed identically:
`… 09 00 00 00 | 01 | 80 00 00 00 | "CLOC" | 04 00 00 00 | <len> …` (`80`=change flags, `CLOC`=4-byte
form-type tag, `04`=version). The one owning our scan list is @body `0x80A12`; its payload @`0x80A5C`
is a list of 3-byte-tag + FormID handles: `67 a5 72`/`67 a5 76` = LocRefType `0x0027A5xx`
(`PlanetTraitScanTargetLocRef`), `61 b2 50` = scan-target ACTI `0x0021B250`, `61 b2 52` = ACTI
`0x0021B252`, etc. So the durable record IS a serialized `BGSLocation` runtime LocRef/ref-tracking blob.
The Papyrus natives `GetLocRefTypes`/`HasLocRefType`/`SetLocRefType` on ObjectReference
(`ID_118314`/`ID_118352`/`ID_118450`, `findrefs-impls:1883/2264/3222`) operate on this same
ref↔Location LocRefType relationship — `SetLocRefType` is the closest WRITE primitive but it sets a
single ref's type, not the Location's encountered-ref list, and it is not what a scan calls.

### C. `ID_90522`/`ID_90523` COUNT SOURCE  `[decompile-verified]` — answers Q2

- **The per-frame outline/count walker** `ID_90521 @14159f240` (off `0x159f240`) → `ID_90522 @14159f480`
  (off `0x159f480`): walks the **global 939118 registry** at `*(db)+0x268` (`onp-resolver:2070-2167`),
  for each entry resolves FormID→REFR (`ID_47401`→vtable `+0x228`), does distance/LOS, and feeds the
  per-entry byte `*(u8*)(entryOff+0x28)` as `param_3` to **`ID_90523 @14159f7a0`** (off `0x159f7a0`).
  So `ID_90522`'s INPUT byte IS `939118+0x28`.
- **BUT `ID_90523` (the per-target state classifier) does NOT decide "scanned" from that byte alone.**
  `onp-resolver:2478-2607`: it computes `local_1ac = ID_83009(ref)` (canonical), `ID_52188(ref,…)`
  (planetId), then **`ID_83041(*(mgr+0x8b0))`** (durable 938333 read-back) → `local_res8`; the state
  is `'d'`-gated (`local_res20 = (local_res8[0]=='d')`) AND known-set-membership-gated
  (`ID_56887(*(937609+0x160)+0x20, …, canon)`, :2508). The final per-target code `local_1b0` is
  `0xc` (unknown) or `(local_res10!=0)+{0,2,4,7}` depending on durable-'d' + known + in-range +
  `param_3`. So `param_3` (the +0x28 byte) only nudges in-range vs scanned-in-range; the dominant
  inputs are the **durable 938333 'd' state** and **known-set membership** — exactly the two the mod
  matched in 938333 yet still reads 0/2, because **neither the 'd' classification nor the on-screen N
  re-derives without the Location/CLOC half being present on reload.**
- **The on-screen "N" digit** (`MonocleUIDataModel model+0xa0`) is written by the painter
  **`ID_90518 @14159b890`** (off `0x159b890`) as `model+0xa0 = (uint)*(u8*)(ID_938422+0x38)`
  (`onp-resolver:270-299`, decompile-verified this pass: `lVar9=ID_938422; … bVar13 = byte@+0x38; if
  (*(uint*)(model+0xa0)!=bVar13){…}`). **`ID_938422` is the scanner-menu state global @ `0x1461ea0c8`;
  its `+0x38` tally is rebuilt only by the scan/aim refresh (`ID_90530`/`ID_90517→ID_90548`), NOT by a
  per-frame recount of `939118+0x28`.** This is why poking `939118+0x28` colors the outline (via
  `ID_90523 param_3`) but never moves "N". (`ID_90518` case 5 = EnvironmentalFeature reveal:
  `ID_83009`→`ID_124900` durable read + **`ID_64337(ref,player,1,0)`** perception gate decides
  Unknown→named, `onp-resolver:1326-1399`.)

### D. VERIFIED Frida hook table — for ONE real-scan capture  `[decompile + caller-xref verified]`

Caller xrefs (`hookfreq-xrefs-2026-06-24.txt`) classify each fn. **Per-frame = the monocle paint loop
reached via `ID_90501` (which calls `ID_90521` + `ID_90517`); NEVER hook these.** Cold = reached ONLY
from the scan-complete UI (`ID_91882`/`ID_91992` → `ID_90506`) or the durable writers.

| name | file-offset | freq | safe? | args reveal |
|---|---|---|---|---|
| **ID_90506** real scan handler | **`0x1599f70`** | **per-SCAN** | ✅ COLD | `rcx`=monocle; read `*(rcx+0xf18)` = aimed ref FormID. Fires once per scan — the master anchor for the whole capture. |
| **ID_83008** scan inner (`ScanRefNative`) | **`0x1307910`** | per-SCAN | ✅ COLD | `rcx`=ref, `rdx`=scanned(1), `r8`=mode(8). Sets `939118+0x28`; the entry point of the durable fan-out. |
| **ID_83038** `939118+0x28` writer | **`0x130a600`** | per-SCAN | ✅ COLD | `rcx`=db, `rdx`=&scannedFlag (`*u8`), `r8`=&formId (`*u32`). Recovers the exact canonical id keyed + confirms the `if(byte changed)` gate. |
| **ID_52157** per-planet credit | **`0x7b7fa0`** | per-SCAN | ✅ COLD | `rcx`=ref/player, `edx`=canonId/FormID, `r8b`=mode. Entry to durable 938333. |
| **ID_52158** durable 938333 writer | **`0x7b81c0`** | per-SCAN | ✅ COLD | `rcx`=&ctx{planetId@0,species@4,…}, `rdx`=&db. Writes `+0x21/+0x20`; this is the durable survey record write. |
| **ID_83025** known-set reveal | **`0x309730`** ⚠ | per-SCAN | ✅ COLD (but verify offset) | `rcx`=biome(`*(937609+0x160)`), `rdx`=ref, `r8d`=canonId. **NB the prior probe_scan.py used `0x309730`; the true decompile address is `141309730` → file-offset `0x1309730`. The old `0x309730` was WRONG (missing the `0x1` nibble) — fix it.** |
| **ID_90530** scanner-state rebuild | **`0x15a08c0`** | per-SCAN | ✅ COLD | `rcx`=monocle. Rebuilds `ID_936593` survey cache + the `ID_938422+0x38` tally inputs; reads `ID_37878(player,0x81)`. Watch this to see the count refresh. |
| ID_90513 scan-complete SFX/UI | `0x159ae80` | per-SCAN | ✅ COLD | `rcx`=monocle. (The earlier "fired 2451×, per-frame" label was a MIS-MEASUREMENT — its callers are `ID_90493`/`ID_90506`/`ID_90528`, all scan/init, NOT a frame tick. Safe, but low signal.) |
| ID_90517 re-aim + repaint | `0x159b730` | **per-FRAME** | ❌ NEVER | called from `ID_90501` (monocle per-frame). Calls `ID_90548` + schedules paint. Hooking jams the UI loop. |
| ID_90521 outline/count walker | `0x159f240` | **per-FRAME** | ❌ NEVER | called from `ID_90501`. Walks the 939118 registry every frame the monocle is up. |
| ID_90522 / ID_90523 | `0x159f480` / `0x159f7a0` | **per-FRAME** | ❌ NEVER | inner of `ID_90521`. |
| ID_90518 painter (N=`938422+0x38`) | `0x159b890` | **per-FRAME** | ❌ NEVER | inner of `ID_90676`/`ID_90661` paint dispatch. |

**Capture recipe (one real scan):** attach to `Starfield.exe`, hook the 7 COLD fns above, aim a
Microbial-Community target, hand-scan once. The trace will show: `ID_90506` (aimed FormID) →
`ID_83008` → `ID_83038` (the canonical id + `byte 0→1`) → `ID_52157`→`ID_52158` (durable 938333 write,
planetId+species) → `ID_83025` (known-set) → `ID_90530` (tally rebuild). If a BGSLocation/CLOC write
exists in the scan path it would appear as a NON-survey call BETWEEN these — but the decompile says it
won't; to catch the Location-discovery write, additionally hook the `ID_571xx` LocationManager inserts
(`ID_57195 @1409288f0`-class, the `db+0x268` `ID_938083` component writer) and/or set a hardware
write-watch on the owning BGSLocation's ref array — that is the genuinely open piece.

### E. Is the CLOC writer directly callable by the mod on a loaded ref? `[verdict]`

**No single clean call, per the decompile.** The survey-DB natives the mod already drives
(`ID_83008/52157/52158/83025`) are exhaustive for the survey-DB domain and are confirmed NOT to write
the Location ref list. The CLOC durable bit is owned by the **`db+0x268` component family `ID_938083`**
(LocationManager `ID_571xx`, keyed by ref FormID) and/or a deferred discovery task — reachable only with
a **loaded ref on the current Location** (materialization-bound), exactly matching the EIGHTH section's
"Location-bound, no ref-free/all-planets pre-write" conclusion. The honest path remains: drive the real
engine scan on the aimed loaded ref (`ID_90506`'s sequence), which is what populates BOTH the survey-DB
AND, via the Location-discovery system the engine runs around it, the CLOC record. **The mod cannot
fabricate the CLOC record from `db+0x268` alone** (`ID_938083` is a *different* component than the mod's
938333/939118 writers, but it IS in the same `db+0x268` container — a future probe could try a
`DbLookup` under disc `ID_938083` keyed by the loaded scan-target ref's FormID to set its scanned/seen
byte, mirroring `MarkScannableKnownDurable`; UNVERIFIED, needs a live ref + the `ID_57195` write shape).

---

## ★★★★★★★★ EIGHTH (2026-06-24, the MOD-vs-REAL save diff — supersedes ALL `938333` gate claims below): 938333 is **byte-identical** mod↔real; the durable difference a real scan makes and the mod does NOT is a **scanned-LocRef list inside a `CLOC` (Location) ChangeForm** — NOT in `db+0x268`, so NOT ref-free writable.

> **This is the first pass to diff the ACTUAL mod save (Save21, v4 with the latest `CompleteScanTargetCredit`)
> against a real 2/2 scan (Save14), instead of an early mod prototype (Save18/19). The result overturns the
> recurring "938333 member-array / `0x00013FE1` known-set is the gap" conclusion for the SHIPPING mod.**
> Reproduce: `py re/save/verify_render_gate.py` (READ-ONLY). Diff tooling: `re/save/region1_diff_fast.py`,
> `re/save/decode_pk_record.py`.

### FACT 1 — `938333` is FULLY reproduced by the mod  `[save-verified]`

The 938333 per-planet PlayerKnowledge record (GlobalData region 1) for Jemison × canonical ACTI
`0x0021B250` is **byte-for-byte identical** between Save14 (real 2/2) and Save21 (mod v4):

```
member+canon window  (16 B before the canonical ^TAG id .. +8):
  Save12 (clean) : 00 b4 29 84 00 2b 23 65 00 a9 2b 84 00 00 00 00 a9 2b 84 00 50 b2 61 00 ad 2b 84 00   (no canon slot)
  Save14 (REAL)  : 00 00 00 00 01 00 00 00 88 55 62 00 01 00 00 00 00 00 00 00 50 b2 61 00 02 64 00 00
  Save21 (MOD)   : 00 00 00 00 01 00 00 00 88 55 62 00 01 00 00 00 00 00 00 00 50 b2 61 00 02 64 00 00   ← == Save14
```

So the mod ALREADY writes, identically to a real scan: the **pooled member array** `count=1, [0x00625588]`
(the trait keyword), the **canonical slot** `0x0061B250` with **flag(+0x21)=2** and **pct(+0x20)=100**.
Every "FIX" in the THIRD..SEVENTH sections below (member array, flag=2, pct=100, known-set byte) is
therefore **already satisfied in the shipping save** — yet the panel STILL shows "0/2 / UNKNOWN FEATURE"
and that persists across reload. **Conclusion: 938333 is necessary-but-not-sufficient and is NOT the
render gate. The persistence the user sees comes from a different durable store.**

### FACT 2 — the durable difference is a scanned-LocRef list in a `CLOC` ChangeForm  `[save-verified]`

Exhaustive diff of GlobalData region 1 isolates only session noise + the (matched) 938333 record. The
ONE durable, scan-caused, mod-missing difference is in the **ChangeForms region** (R3 = `body[offsetC:cf]`):
a real scan **appends a list of typed references inside a `CLOC` (Location) ChangeForm**. The list (S14
@body 0x80A83, 113 B after a `CLOC` tag `80 00 00 00 | CLOC | 04 00 00 00` @0x80A12) reads:

```
… 84 2b a9 43 f7 e8 46 15 81 41 ab 29 | 61 b2 50 00 00 00 | 41 ab 29 00 00 00 | 46 15 81 00 00 00 |
  67 a5 76 00 00 00 | 62 55 63 00 00 00 | 61 b2 52 00 00 00 | 11 81 00 00 00 …
```

`61 b2 50` = the scan-target ACTI handle (`0x0021B250`-class); `67 a5 76` = a LocRefType-framed handle
(the `PlanetTraitScanTargetLocRef 0x0027A567` family); `61 b2 52` = the adjacent ACTI `0x0021B252`.
Presence of the scan-target handle `61 b2 50` inside this CLOC list, by scan progress:

| save | scan state | scan-target handle `61 b2 50` in CLOC | the full distinctive list |
|---|---|---|---|
| Save12 | 0/2 clean | **0** | absent |
| Save13 | 1/2 (1 real scan) | **1** | (appears at first scan) |
| Save14 | 2/2 (2 real scans) | **1** | **present** |
| **Save21** | **MOD v4** | **0** | **ABSENT** ← the mod never writes it |

So a real scan **registers the scan-target reference into the current Location's (CLOC) changed-form**;
the mod's knowledge-DB write never does. This is durable (a saved ChangeForm), reloads with the Location,
and references the scan-target — exactly the missing half that makes the Location/monocle treat the
scan-target as discovered. (Honest caveat: the handle count is 1 at both 1/2 and 2/2 — it is the
Location's *discovered-reference membership*, not a per-scan 0→2 counter. The literal "N/M" digit is
rebuilt per-frame from the loaded LocRef refs' transient `939118+0x28` bytes — `[decompile]`
`trait-onplanet-completion-2026-06-23.md §1c`, `ID_90522` walk — and is not itself a durable scalar.)

### FACT 3 — this gate is NOT ref-free / NOT `db+0x268`-writable  `[decompile+save]`

A `CLOC` ChangeForm is a **per-visited-Location changed-form**, not a BSComponentDB2 StoredComponent in
`db+0x268`. The mod's `GetKnowledgeDB`/`DbLookup`/`ResolvePlanetSubobj` machinery operates ONLY on
`db+0x268` (where 938333 and 939118 live) and **cannot reach a CLOC ChangeForm**. There is therefore
**no ref-free, all-planets pre-write** that reproduces this. It requires the engine, on the loaded
Location with the loaded scan-target ref, to register the discovered reference — i.e. the real on-planet
scan (or a faithful native replay of it on the aimed/loaded ref). This is consistent with — and now
**save-confirms** — the standing decompile verdict that trait-scan-target completion is
**materialization- and Location-bound** (`trait-true-completion-2026-06-23.md §4`,
`trait-onplanet-completion-2026-06-23.md §1c/§6`). The `+0x54 ↔ ID_52188` planet-key subtlety
(`render-read-target-2026-06-22.md`) is NOT the cause here (938333 matches), so the planet key is fine.

### Decompiled readers that prove the panel reads beyond 938333  `[decompile-verified]`

- **Name reveal** (panel populate `ID_90518` **case 5** = EnvironmentalFeature, the trait scan-target):
  `trait-ref-count.txt:1326-1398`. It (a) reads 938333 via `ID_124900(record+0x20, canonId, out)` —
  which reads the per-canonical record's **member vector at +0x08/+0x10** (`tc-q2:1-17`; `ID_124900`→
  `ID_37875` = `std::vector<u32>::insert` of `[+0x08,+0x10)`) → MATCHED by the mod; THEN (b) gates the
  Unknown→named flip on **`ID_64337(ref, player, 1, 0)`** (`known-set-gate-2026-06-24.txt`) — a runtime
  **perception/known-state** gate that consults the loaded ref's 3D + detection state (`ref+0x45`,
  `ID_883420`), NOT a durable 938333 field. So even with 938333 complete, the reveal needs the ref
  loaded+perceived+registered — which the CLOC/Location registration (FACT 2) underpins on reload.
- **Durable read-back** `ID_83041` (`durable-readers-2026-06-24.txt:104-151`): reads BOTH the 938333
  per-canonical `+0x20` pct byte AND the `+0x08/+0x10` member vector — both matched by the mod. Confirms
  the 938333 domain is fully satisfied and is not the discriminator.

### What this means for the mod (honest)

- **Do NOT** keep adding 938333 / known-set writes for the reveal — Save21 proves they are already
  byte-complete and the panel still rejects. That avenue is exhausted.
- The durable gate (CLOC Location discovered-reference registration) is **on-planet / loaded-ref bound**.
  The only faithful way to set it is the engine's own scan on the aimed/loaded scan-target ref (the
  `ID_90506` sequence on the FormID-correct loaded REFR, `trait-onplanet-completion-2026-06-23.md §5`).
  There is **no ref-free, all-planets write** for it — the per-ref/per-Location transient + changed-form
  nature is the wall, now save-confirmed.
- **READ-ONLY in-game confirmation** the user can run safely (no write, no Frida): via the mod's existing
  `DbLookup` over `db+0x268`, look up 938333 for `(ID_52188(player) planetId, canonical 0x0021B250)` and
  log `slot+0x21`, `slot+0x20`, and whether the pooled member array contains `0x00225588`. Expected on a
  mod save: `+0x21==2`, `+0x20==100`, member present — i.e. ALL satisfied — while the panel still reads
  0/2 / UNKNOWN. That live result *proves on the user's machine* that 938333 is not the gate and the
  remaining difference is the Location/CLOC registration `DbLookup` cannot reach. (The offline
  `py re/save/verify_render_gate.py` already demonstrates this byte-exactly on the three saves.)

---

## ★★★★★★★ SEVENTH (THE REAL STORE, 2026-06-24 PM): the `0x00013FE1` records are the **939118 ScannableComponent** DB StoredComponents — `db+0x268`, disc `ID_939118`, scanned-byte at `component+0x28`; `ID_83008→ID_83038` is the durable writer. biome+0x20 and `ID_83025` are RULED OUT (transient mirror).

> **SOLVED — supersedes the FIFTH/SIXTH sections AND the SECOND/THIRD/FOURTH `938333` corrections for the
> render gate.** A fresh, decisive in-process probe (`ProbeKnownMember`) returned `found=false,
> bucketCount=0` on `biome+0x20` EVEN IMMEDIATELY AFTER A REAL MANUAL SCAN — so `biome+0x20` is the
> TRANSIENT species mirror, NOT the durable store. Full decompile of the disc-registrar, the deserializers,
> and the scan-write path proves the durable `0x00013FE1` records are the **`939118` ScannableComponent**
> StoredComponents, and the **scan writes them durably via `ID_83038`**. The mod can write them ref-free
> with its EXISTING `DbLookup`/`GetKnowledgeDB` machinery — same as `938333`, different disc + key.

### What `0x00013FE1` actually is `[decompile+save-verified]`

`0x00013FE1` is **NOT a discriminator** and **NOT a name hash**. Confirmed by `FindScalar 0x13fe1`:
**ZERO instructions** in the entire binary reference the scalar `0x00013FE1` — it is pure serialized
**data**, a recurring hash/marker value inside the serialized records (it also recurs intra-record at
+0x21, save-verified: `parse_ppbd_record.py`). The real BSComponentDB2 discriminators (`ID_938333`,
`ID_939118`, `ID_938158`, …) are **small runtime u16 slot indices** assigned by registration order in
`ID_124590` (`disc-registrar`: `ID_126671(factory, slot, &discGlobal, "TypeName", 0)` sets
`*(u16*)discGlobal = slotIndex`). So the prior agents' "disc `0x00013FE1`, 31 records" was a 4-byte
pattern-match coincidence; the records are real, but `0x00013FE1` is a field VALUE, not the type tag.

### The store is the `939118` ScannableComponent in `db+0x268` `[decompile-verified]`

The render path and the save BOTH key on `939118`:
- **`ID_83042`** (durable name-reveal helper) and **`ID_83043`** (the LOAD-path deserializer,
  `durable-readers-2026-06-24.txt`/`scannable-serial-2026-06-24.txt`) rebuild a
  `StoredComponent<1, ScannableComponent>` into `db+0x268`, keyed
  `local_res10 = CONCAT24(ID_939118, formId@+0x20) << 0x10`, with the **scanned byte at
  component+0x28** (`*(puVar8+5)=0` init; `cVar3@param_2+0x14` → `*(slot+0x18)=1` when set).
- **`ID_83038`** (the scan inner; `scanned-state.txt:118`) — the DURABLE WRITER. `ID_83008`
  (`ScanRefNative`) does:
  ```c
  local_28[0] = *(u32*)(ref + 0x28);                  // the ref's ScannableComponent canonical formId
  ID_83038(GetKnowledgeDB(), &scannedFlag, &formId);  // db = manager+0x8b0
  ```
  and `ID_83038`:
  ```c
  key = CONCAT24(ID_939118, formId) << 0x10;          // (939118-disc << 48) | (formId << 16)
  ID_126806(db + 0x268, out, &key, ...);              // DbLookup in db+0x268  (SAME container as 938333)
  comp = out.entryBase + *(u16*)(out.entryBase + 0x12 + out.idx*4);
  if (scannedFlag != *(u8*)(comp + 0x28)) *(u8*)(comp + 0x28) = scannedFlag;  // DURABLE write of +0x28
  ```
- **`ID_83007`** (`IsBiomeRef`) READS that same `*(u8*)(comp + 0x28)` (`scanned-state.txt:1`).

So the durable `0x00013FE1` record = the serialized `939118` ScannableComponent; the **save's "+6 KNOWN
byte" ⇄ `component+0x28` scanned byte**; the per-record id (`0x00DD0A84`/flip `0x0063CE83`) = the
component's **canonical scannable formId** (`formId@+0x20`, the `^0x00400000`-tagged DB index). The
mod's `938333` path uses the IDENTICAL container `db+0x268` and the IDENTICAL `DbLookup` (ID_126806) —
only the **disc** (`939118` vs `938333`) and the **key low half** differ.

### Why biome+0x20 / `ID_83025` are RULED OUT `[decompile+probe-verified]`

`ID_83025` (the mod's `RevealKnownNative`, `known-set-create-2026-06-24.txt:448`) does
`ID_56887(param_1 + 0x20, …)` and clears+refills a marker array — but `param_1 = *(ID_937609+0x160) =
the LIVE biome`. It writes ONLY `biome+0x20`, the **transient member mirror that `ID_83036` rebuilds
from the DB on every materialize**. It NEVER touches `db+0x268`. That is exactly why the live probe
found `biome+0x20` empty after a real scan AND why a mod write through `ID_83025` does not persist:
the transient edit is discarded and regenerated on reload. **Stop calling `ID_83025`/`ID_83030`/`ID_83024`
for durability** — they are the wrong domain.

### THE RENDER GATE (reconciled) `[decompile-verified]`

The on-screen panel painter `ID_90518` case 3/4 (`onp-resolver:679-731`) reads the TRANSIENT
`biome+0x20` for the live in-session reveal (so the live name only shows when the biome is materialized
AND the member is present). The DURABLE reload render is driven by the `939118` ScannableComponent's
`+0x28` byte: on load `ID_83043` restores it into `db+0x268`; on materialize `ID_83036`/`ID_83026`
rebuild `biome+0x20` from the loaded components; the panel then reads `biome+0x20`. So **the byte the
save persists is `939118 +0x28`, and reproducing it is what makes the completed/named panel reload**
(Save14 = 1 flipped, Save21 = 0 flipped, `938333` byte-identical between them — `compare_save21.py`).

### THE MOD-WRITABLE RECIPE (A) — ref-free `db+0x268` write, mod's own machinery

Add ONE durable write that mirrors `ID_83038`, reusing `GetKnowledgeDB`/`DbLookup`/`ResolvePlanetSubobj`
internals already in `src/Main.cpp`. The DB key is `(939118-disc << 48) | (canonScannableId << 16)`:

```cpp
// disc 939118 is ALREADY exposed: ScannableDiscriminator {REL::ID(939118)} (uint16* runtime disc).
// Set the DURABLE scanned byte (component+0x28) for a canonical scannable id, ref-free.
bool MarkScannableKnownDurable(std::uint32_t canonScannableId, std::uint8_t scanned = 1)
{
    const auto db = GetKnowledgeDB();
    if (!db || !canonScannableId) return false;
    const std::uint16_t disc = *ScannableDiscriminator.get();             // ID_939118 (NOT 0x00013FE1)
    const std::uint64_t key  = (static_cast<std::uint64_t>(disc) << 48)
                             | (static_cast<std::uint64_t>(canonScannableId) << 16);
    std::uintptr_t out[4] = {0,0,0, kDbLookupNotFound};
    auto container = reinterpret_cast<std::uintptr_t*>(db + kDbContainerOffset);  // db+0x268
    DbLookup(container, out, &key);                                       // ID_126806
    if (out[3] == kDbLookupNotFound && out[2] == 0) return false;        // component not present (planet not loaded)
    const auto base = reinterpret_cast<std::uint8_t*>(out[2]);
    const auto comp = base + *reinterpret_cast<std::uint16_t*>(base + kBucketOffsetTableOff + out[3]*4);
    *(comp + 0x28) = scanned;                                            // the DURABLE scanned byte (== save +6)
    return true;
}
```

- **Key reconciliation (Q4):** the save stores the canonical scannable id (`formId@+0x20`,
  `0x00xxxx83/84` after the `^0x00400000` tag); the in-memory DB key uses the **untagged** id
  (`ID_83009(ref)` / `ref+0x28`). The serializer applies the tag on the way to disk, exactly like
  `938333` keywords. So pass the **untagged** canonical id (`ID_83009(ref)` = the same value
  `ID_83038` keys on). DbLookup miss ⇒ that component isn't loaded for the current planet.
- **THE TRANSIENT-KEY CAVEAT (decisive, why this is NOT all-planets):** the per-record id is a
  **per-session canonical/instance id** (`0x0083CE63` sess 43 vs `0x0083CE71` sess 45 — save-verified),
  NOT a stable base ACTI. The mod's `kTraitScanTargets` base ACTIs (`0x0021Bxxx`) are a DIFFERENT
  domain. So you CANNOT precompute the DB key offline → **the durable `939118` write is on-planet /
  loaded-component bound** (need a live ref to read `ID_83009`, or enumerate the loaded `939118`
  registry). This matches the species model: durable id-reveal is materialization-bound.
- **Easiest correct driver:** the mod ALREADY does the right thing on the on-planet loaded ref via
  `ScanRefNative(ref,1,8,0)` = `ID_83008` → `ID_83038` (durable +0x28). The known FAILURE is the
  **byte-transition gate inside `ID_83038`** (`if (scannedFlag != current)`) + that `ID_83008` only
  reaches `ID_83038` when the ref carries a live `939118` component. So:
  1. Keep `ScanRefNative(ref,1,8,0)` for the LIVE outline, but DON'T rely on it for the durable byte if
     an earlier pass already set+cleared it.
  2. Add the **direct `MarkScannableKnownDurable(ID_83009(ref), 1)`** above on each loaded scan-target
     ref (from the registry walk `ForEachScannableInRegistry`, which already yields exactly the loaded
     `939118` formIds) so the durable `+0x28` is set unconditionally, no transition gate, no spawn.
  3. `938333` (the existing `CompleteTraitSlot`) STILL needed for "100% SCANNED" — it is byte-correct
     already (Save21 == Save14 on `938333`). The ONLY missing durable byte is this `939118 +0x28`.

### Frida confirmation hook (C) — capture the live key + offset during a real scan (game wasn't running)

Hook the COLD per-scan writer `ID_83038` (`@14130a600`, file-offset `0x130a600`, already in
`re/frida/probe_scan.py`). It is per-scan, never per-frame — safe. Log: `args[0]`=db,
`args[1]`=&scannedFlag (read `*u8`), `args[2]`=&formId (read `*u32`) → recovers the exact canonical id
the scan keys, and `Memory.readU64` of `(db+0x268)`-lookup confirms `component+0x28` flips. Also hook
the deserializer `ID_83043` (`@14130ad90`, file-offset `0x130ad90`) on load to dump `param_2+0x14`
(scanned byte), `param_2+0x20` (formId) for every restored `939118` record — that prints the 31 ids and
which one has the byte set, mappable 1:1 to the `0x00013FE1` save records. (frida 17.15.3 installed;
Starfield.exe was NOT running this session, so this is the design, not a captured trace.)

`[decompile-verified]` registrar `ID_124590`/`ID_126671` (disc = runtime slot, not 0x00013FE1);
deserializers `ID_83043`/`ID_124652` keyed `CONCAT24(ID_939118|ID_938158, …)<<0x10` into `db+0x268`;
durable writer `ID_83008`→`ID_83038` writes `component+0x28`; `ID_83007` reads it; `ID_83025` writes
ONLY transient `biome+0x20`. `[save-verified]` `compare_save21.py` (Save14 1 flipped / Save21 0,
`938333` identical); `parse_ppbd_record.py` (`e1 3f`/`0x3FE1` recurs as DATA at +0 and +0x21, id1 =
per-session canonical, +6 = scanned byte); `find-13fe1-2026-06-24.txt` (0 instructions ref 0x13fe1).
Dumps: `durable-readers-2026-06-24.txt`, `scannable-serial-2026-06-24.txt`, `disc-registrar-2026-06-24.txt`,
`ppbd-storedcomp-2026-06-24.txt`, `scanned-state.txt`, `re/save/probe_*.py`/`parse_ppbd_record.py`.

---

## ★★★★★★ SIXTH (THE LAST BYTE, 2026-06-24): +0x06 = "marker array non-empty"; `ID_83025` IS the writer; `ID_83030` (CREATE) was WRONG

> **⚠ SUPERSEDED by the SEVENTH section above.** This section concluded the durable byte is the
> `biome+0x20` marker-array projection written by `ID_83025`. The live probe (`biome+0x20` empty after a
> real scan) and the full decompile (the durable store is the `939118` ScannableComponent in `db+0x268`,
> written by `ID_83038`; `ID_83025` only edits the TRANSIENT `biome+0x20` mirror) DISPROVE that. Do NOT
> call `ID_83025`/`ID_83030`/`ID_83024` for durability. Retained below for history.

> **SOLVED — supersedes the FIFTH section below.** New save fact (`re/save/compare_save21.py`): the
> `0x00013FE1` known-set table has **31 records in EVERY save** (Save12 unscanned, Save14 real, Save21
> mod) — **the members PRE-EXIST; a scan adds none.** A real scan flips **one** record's `rec+0x06`
> 0→1. So the FIFTH section's premise ("the member is missing → CREATE it with `ID_83030`") is FALSE,
> and `KnownSetCreate`/`ID_83030` is unnecessary (at best a no-op; at worst it inserts a duplicate,
> markerless member that never flips `+0x06`). **The real gap is filling the existing member's marker
> array — which `ID_83025` already does.**

### The in-memory member has NO `+0x06` byte — `+0x06` is a SERIALIZER projection `[decompile-verified]`

The live member at `knownSet+0x20` (`*(ID_937609+0x160)+0x20`) is a `BSTScatterTable<u32, uint[]>`,
**0x28-byte entries**, fields: `key@+0x00 (=canonId)`, `markerArray{begin@+0x08, end@+0x10, cap@+0x18}`,
`chainNext@+0x20`, `home@+0x24`. **There is no scalar byte at member+0x06.** The save record's geometry
`e1 3f 01 00 | 00 00 | KNOWN@+0x06 | 01 00 00 00 | clusterId@+0x0B` is the **per-cluster StoredComponent
serialization** of this object, NOT a raw dump of the 0x28 entry. The serializer emits, per member:
- `KNOWN-BYTE @rec+0x06` = **`(member.markerArray non-empty)`**, i.e. `member+0x08 != member+0x10`.
- `01 00 00 00 @rec+0x07` = a BSTArray **count = 1** of cluster handles.
- `clusterId @rec+0x0B` = the **cluster handle** (`*(ID_937609+0x80)`-domain id) the materializer
  associated with this member; looked up from the canonId at serialize time (transient per-session:
  `0x0063CE83` sess.43, `0x0083CE71` sess.45 — NOT a stable FormID).

So **rec+0x06 ⇄ "member.markerArray non-empty"** and **rec+0x0B ⇄ clusterId (separate stored field)**;
the in-memory key stays the canonId. This is why a real scan flips exactly one byte: it populates one
member's marker array.

### What sets it on a real scan `[decompile-verified]`

`ID_90506` (tc-q3, the live scan-complete handler) → at HIT it does
**`ID_83025(knownSet, ref, canonId)`** (`known-set-create-2026-06-24.txt:171, 207`; also the gate
`ID_90518` cases 3/4 at `onp-resolver:708` and `:2520`). `ID_83025` (`@141309730`):
```c
void ID_83025(knownSet, ref, canonId) {
    ID_56887(knownSet+0x20, &out, &canonId);          // FNV-find member by canonId
    if (HIT /* out[1]!=knownSet+0x48 || out[0]!=knownSet+0x40 */) {
        member = *out[0] + out[1]*0x28;
        *(member+0x10) = *(member+0x08);              // markerArray.end = begin  (clear)
        src = (ref+0x2e=='K') ? ID_909810 : ID_909812;  // runtime marker-source registry
        if (src) ID_83024(src, ref, member+0x08);     // REFILL markerArray (appends *(srcEntry+0x28) ids)
    }
}
```
`ID_83024` (`@141309590`) walks the source registry (`src+0x38` count, `src+0x88`/`+0x50`/`+0x40`/`+0x60`
tables) and **appends marker ids into `member+0x08..+0x10`**. So after `ID_83025` on a HIT, the member's
marker array is **non-empty** → serializes `rec+0x06 = 1`. **A direct member-byte write does NOT
reproduce it** because there is no such byte; the durable flip is *derived* from the marker array, so the
mod must fill the marker array (i.e. call `ID_83025`), not poke a byte.

### Key reconciliation (Q4) `[decompile-verified]`

The find-key and the render-key are the **same** value: `ID_83009(ref)` returns the ScannableComponent
canonical (`939118`-component `entry+0x24`) or `ref->formID`, and BOTH the render gate
(`onp-resolver:698,2506`) and `ID_83025` key `knownSet+0x20` by that exact id. So
`FindBiomeMember(canon)` with `canon = ID_83009(ref)` resolves the same member the gate reads and the
serializer emits. The pre-existing member is findable by canonId.

### THE SAFE MOD WRITE (corrected — drop `ID_83030`, keep `ID_83025` on the loaded ref)

The member pre-exists, so the **only** required write is `ID_83025` on a LOADED, on-planet ref. The mod
ALREADY ships this as `RevealKnownNative` (`REL::ID(83025)`). Fix `RevealScanTargetIdentity` to **remove
the `KnownSetCreate(ID_83030)` line** (wrong per the save fact) and keep just the mark:
```cpp
void RevealScanTargetIdentity(RE::TESObjectREFR* ref)
{
    if (!ref) return;
    auto* const s = Singleton937609.get();
    if (!s || !*s) return;
    const auto biome = *reinterpret_cast<std::uintptr_t*>(*s + 0x160);
    if (!biome) return;                              // off-planet / no live biome -> nothing to mark into
    const std::uint32_t canon = GetCanonicalSpeciesId(ref);   // ID_83009 (== the gate/serializer key)
    if (!canon) return;
    // OPTIONAL pre-check (purely diagnostic; ID_83025 already no-ops on MISS):
    //   std::int64_t out[2]={0,0};
    //   FindBiomeMember(static_cast<std::int64_t>(biome)+0x20, out, reinterpret_cast<unsigned char*>(&canon));
    //   if (out[1] == *(int64*)(biome+0x48) && out[0] == (int64)(biome+0x40)) return;  // MISS -> not a known-set member
    RevealKnownNative(biome, ref, canon);            // ID_83025: fills member.markerArray -> serializes rec+0x06=1
}
```
- **Why this is THE write:** `ID_83025`→`ID_83024` populates `member+0x08..+0x10`; the serializer projects
  that to `rec+0x06=1`. No allocation, no byte-poke — `ID_83024` appends into the member's existing
  BSTArray via the engine path. **Single-byte member write is impossible/wrong** (no such field); fill the
  marker array instead. Heap-safe: `ID_83024` uses `ID_35755`-class engine growth on the member's own
  array (the array the serializer/loader round-trip).
- **`CompleteScanTargetCredit` already calls `RevealScanTargetIdentity(ref)`** (Main.cpp:485) — once the
  `ID_83030` line is dropped, the existing `ID_83025` call flips the byte (Save14-equivalent). No new
  REL::ID needed; **remove** `KnownSetCreate {REL::ID(83030)}` and its call.
- **Caveat (unchanged):** on-planet / loaded-ref / live biome ONLY (`biome != 0`). The member is
  materialized with the biome; off-planet `knownSet` is null so nothing to mark — same materialization
  bound as the species model. The ref-free `CompleteTraitSlot` path persists the `938333` survey-%, but
  the `+0x06` reveal stays on-planet-bound.

`[save-verified]` `compare_save21.py` (31 records in all saves; 1 flipped in Save14, 0 in mod).
`[decompile-verified]` member layout has no +0x06 (`known-set-create-2026-06-24.txt:450-477`,
`56887`); `ID_83025`→`ID_83024` fill markers (same file); gate reads markers
(`onp-resolver:697-730, 2504-2537`); `ID_83009` keys = gate keys (`known-set-canon-keying-2026-06-24.txt`);
the `938333`/`939118` serializers are SEPARATE families (`known-set-serializer-decomp-2026-06-24.txt` =
`ID_52193`/`ID_45726` for 938333, `ID_83043`/`ID_83046` for the 939118 ScannableComponent).

---

## ★★★★★ FIFTH (THE LAST PIECE, 2026-06-24): the known-set CREATE primitive is `ID_83030` — `ID_83025` only MARKS

> **⚠ SUPERSEDED by the SIXTH section above (2026-06-24 PM).** This section concluded the member is
> MISSING and must be CREATED via `ID_83030`. The `compare_save21.py` save fact (31 members in EVERY
> save, incl. unscanned) DISPROVES that: the members PRE-EXIST. `ID_83025` no-ops not because the member
> is missing but only when off-planet/unmaterialized; on-planet the member exists and `ID_83025` fills
> its marker array (the +0x06 flip). DO NOT add `ID_83030`. Retained below for history.

> **SOLVED.** The reason the mod's `RevealScanTargetIdentity` (`ID_83025`) never flips a KNOWN-BYTE
> (Save19 = 0 flipped vs real Save14 = 1) is that **`ID_83025` no-ops on a MISS** — it `ID_56887`-finds
> the canonId in `knownSet+0x20` and only marks if the member already EXISTS. A trait scan-target's
> base ACTI is **never** a member of the biome known-set (that set holds biome SPECIES, populated at
> biome-materialize time by `ID_83036`), so the find always misses → `ID_83025` returns without writing.
> **The member-CREATE primitive is `ID_83030`** (`@141309aa0`), the engine's own insert-on-miss for this
> exact hashmap. New decompiles: `re/ghidra/output/known-set-create-2026-06-24.txt`,
> `known-set-insert-2026-06-24.txt`, `known-set-allocator-2026-06-24.txt`,
> `known-set-materializer-2026-06-24.txt`, `known-set-xrefs-2026-06-24.txt`.

### Who CREATES a known-set member (the function) `[decompile-verified]`

A real scan never creates the member during the scan; the member is created when the **biome
materializes**. `ID_83026` (`@1413097d0`, the biome known-set rebuild, vtable-invoked on attach) calls
**`ID_83036`** (`@14130a3a0`), which range-walks the planet's loaded scan entries (via `ID_47749`) and
for each marker id does `ID_56887`-find; **on MISS** it calls **`ID_83032`** (the slot allocator) then
writes the new entry — `*entry = id; zero entry+0x08..0x28` (`known-set-insert-2026-06-24.txt:251-258`).
The clean, single-key wrapper of that create is **`ID_83030(hashmap, &id)`** (`@141309aa0`):
```c
longlong ID_83030(longlong hashmap /* = knownSet+0x20 */, undefined4 *keyId)
{
    local_res8 = *(hashmap + 0x28);                 // current capacity
    cVar2 = ID_83032(hashmap, &local_res8, keyId);  // find-or-alloc slot (grows via ID_83033 if full)
    if (cVar2 != '\0') {                            // newly created
        slot = *(hashmap + 0x20) + local_res8 * 0x28;
        *slot = *keyId;                             // key = the canonical id
        *(slot+0x08) = *(slot+0x10) = *(slot+0x18) = 0;  // empty marker BSTArray {begin,end,cap}
    }
    return *(hashmap + 0x20) + local_res8 * 0x28 + 8;  // -> &slot.markerArray (for ID_83024 to fill)
}
```
`ID_83032`'s grow path uses the **engine BSTArrayHeapAllocator** (`ID_83033`→`ID_123792(&ID_883289,
n*0x28,8,1)`, `known-set-allocator-2026-06-24.txt:144`) and rehashes via `ID_74383` — the same heap the
serializer (disc `0x00013FE1`) and load path round-trip, so the inserted member is engine-owned and
persists (it is exactly the byte Save14 has and Save19 lacks). **This is the heap-safe CREATE** — no
hand-rolled allocator poke; it is the engine's own `BSTScatterTable` insert.

### The known-set in-memory structure (decompile-verified)

`knownSet = *(ID_937609 + 0x160)` (the live biome's discovered-set). The **member hashmap base** is
`knownSet + 0x20` — a `BSTScatterTable<u32 key, {u32[] markers}>` (CommonLibSF `RE::BSTScatterTable`),
FNV-1a keyed on a 4-byte id, **0x28-byte (40) entries**, `-1` empty sentinel. Fields relative to the
hashmap base `H = knownSet+0x20` (from `ID_56887`/`ID_83032`/`ID_83026`):

| field | addr (rel knownSet) | meaning |
|---|---|---|
| entries base | `*(knownSet+0x40)` (= `*(H+0x20)`) | array of 0x28-byte entries |
| capacity | `*(knownSet+0x48)` (= `*(H+0x28)`) | bucket count; also the MISS/end sentinel value |
| free count | `*(knownSet+0x50)` (= `*(H+0x30)`) | slots remaining before grow |
| last-free idx | `*(knownSet+0x58)` (= `*(H+0x38)`) | reprobe cursor |

Each **entry** (stride `0x28`):

| off | type | meaning |
|---|---|---|
| `+0x00` | u32 | **KEY = the canonical id** (`ID_83009(ref)` = the scan-target base ACTI for traits) |
| `+0x08` | u64 | marker BSTArray `begin` (the `uint[]` ids `ID_83024`/`ID_83025` splice into the detail panel) |
| `+0x10` | u64 | marker BSTArray `end` |
| `+0x18` | u64 | marker BSTArray `cap` |
| `+0x20` | u32 | chain-next index (`-1` = empty slot sentinel) |
| `+0x24` | u32 | home index |

`ID_56887(H, out[2], &key)` returns `out[0]=H`, `out[1]=index`; **MISS** ⟺ `out[1]==*(knownSet+0x48)`
**and** `out[0]==knownSet+0x40`. The render gate (`ID_90518` case 3/4, onp-resolver:695-731) reveals the
real name **on membership alone** (the sentinel check at :700-703) — the marker array contents drive the
detail-panel marker splice, not the name boolean. So a **bare member created by `ID_83030` reveals the
name**; the subsequent `ID_83025` then fills its markers for the splice.

**Reconciling the SAVE key (clusterId@+0x0B) vs the render key (canonId):** the in-memory entry is keyed
by the canonId (`entry+0x00`). The serializer (disc `0x00013FE1`) writes a per-cluster record; the
KNOWN-BYTE@rec+0x06 is the discovered flag and clusterId@rec+0x0B is the save-side cluster handle. On
load the engine rebuilds the canonId↔record mapping through the biome's cluster→species table (the same
`ID_83026` materialize pass), so the durable cluster record re-projects onto the canonId-keyed member —
which is why the byte persists yet the in-memory lookup is by canonId.

### THE SAFE ADD RECIPE (heap-safe, engine-call, on-planet loaded ref)

Add two engine REL::IDs to the mod and replace the no-op `ID_83025`-only reveal with **create-then-mark**:
```cpp
// ID_83030: known-set member CREATE (insert-on-miss into the BSTScatterTable at knownSet+0x20).
//   Engine-owned grow (ID_83033 -> ID_123792 BSTArrayHeapAllocator); persists under disc 0x00013FE1.
using fn_known_create_t = std::int64_t (*)(std::int64_t hashmap, std::uint32_t* keyId);
inline REL::Relocation<fn_known_create_t> KnownSetCreate {REL::ID(83030)};   // @141309aa0

void RevealScanTargetIdentity(RE::TESObjectREFR* ref)   // REPLACES the ID_83025-only body
{
    if (!ref) return;
    if (IsBiomeRef(ref) == 0) return;                    // ID_83007 guard: live 939118 component (§7)
    auto* const s = Singleton937609.get();
    if (!s || !*s) return;
    const auto biome = *reinterpret_cast<std::uintptr_t*>(*s + 0x160);
    if (!biome) return;                                  // off-planet / no live biome -> can't reveal
    std::uint32_t canon = GetCanonicalSpeciesId(ref);    // ID_83009 (scan-target base ACTI)
    if (!canon) return;
    // 1) CREATE the member if missing (idempotent: ID_83030 returns existing slot, no dup).
    KnownSetCreate(static_cast<std::int64_t>(biome) + 0x20, &canon);   // <-- the missing half
    // 2) MARK it discovered + splice its detail-panel markers (now that the member EXISTS).
    RevealKnownNative(biome, ref, canon);                // ID_83025 (no longer a no-op)
}
```
- **Why heap-safe:** `ID_83030`→`ID_83032`→`ID_83033`→`ID_123792` is the engine's own
  `BSTScatterTable` insert + `BSTArrayHeapAllocator` grow — the exact path the materializer uses. No
  manual alloc, no hand-poked bucket array (the thing that crashed the heap before). CommonLibSF's
  `RE::BSTScatterTable` insert is the equivalent typed call if you prefer not to add the REL::ID, but
  the raw `ID_83030` is simpler and identical at runtime.
- **Latch/lock:** the materializer `ID_83026` takes a manager refcount (`ID_126655`/`+0x238`) around its
  walk, but the **single-key `ID_83030` insert needs no lock** — `ID_83032` mutates only the hashmap
  fields and the engine heap, and we call it on the main thread inside the already-quiescent on-planet
  completion path. Keep the §7 guards: `ID_83007(ref)!=0`, scan-target base form-type, cell-attached,
  on-surface. Do NOT call off-planet (biome null) — there is no known-set to insert into.
- **Then route trait completion through this**, not the ref-free `CompleteTraitSlot` alone:
  `CompleteScanTargetCredit` already calls `RevealScanTargetIdentity(ref)` (Main.cpp:471) — with the
  CREATE added it now actually flips the KNOWN-BYTE → Save will match Save14 → reloads as named.
- **Off-planet / ref-free** (`CompleteTraitSlot`, no loaded ref, biome null): still cannot insert
  (no live `knownSet`); the durable `938333` slot write persists the survey-% credit, but the
  Unknown→named reveal stays on-planet-bound exactly as the species model. The honest minimal win is:
  **add `ID_83030` to the on-planet path** so the one byte Save14 has is finally written.

`[decompile-verified]` `ID_83030`/`ID_83032`/`ID_83033`/`ID_74383` (`known-set-allocator-2026-06-24.txt`),
`ID_83036`/`ID_83026` create-during-materialize (`known-set-insert/-materializer-2026-06-24.txt`),
`ID_83025` mark-only (`known-set-create-2026-06-24.txt:448-477`), xrefs proving `ID_83030`/`ID_83036`
are the ONLY known-set inserters (`known-set-xrefs-2026-06-24.txt`, `known-set-83036-callers-2026-06-24.txt`).
`[save-verified]` Save14 has 1 KNOWN-BYTE, Save19 (mod, `ID_83025`-only) has 0 — the CREATE is the gap.

---

## ★★★★ THIRD CORRECTION (2026-06-24): the gate is the KNOWN-SET (disc `0x00013FE1` / `ID_937609+0x160`), NOT `938333`

> **The `938333` record is NOT the render gate.** Save14 (real, renders "100% SCANNED" +
> "MICROBIAL COMMUNITY" on reload) and Save19 (mod `TestTraitRegistryWalk`→`CompleteTraitSlot`,
> renders "0/2" + "UNKNOWN FEATURE") have a **byte-identical `938333` per-planet subobject** — the
> scanned slot (`50 b2 61 00 02 64`, flag=2 pct=100), the pooled keyword (`88 55 62 00`), the slot
> hashmap, the second knowledge list at R1+0x535xx (`0x0066B066`/`0x0066B048`), AND the per-biome
> "attribute-known" bytes (the `01` adjacent to the canonical, keys `0x00842xxx`/`0x00843xxx`) all
> match. The mod faithfully reproduced every `938333`-domain field. **The single durable difference
> that gates the named/complete reload render is a SEPARATE store the mod never touches.**

### THE DURABLE DIFFERENCE (byte-exact, `[save-verified]`)

GlobalData **region 1** holds a **31-entry table** (= the 31 trait scan-target / biome-cluster
records) discriminated by **`0x00013FE1`** (LE `e1 3f 01 00`). Each record:

```
e1 3f 01 00 | 00 00 | <KNOWN-BYTE u8 @rec+0x06> | 01 00 00 00 | <clusterId u32 @rec+0x0B> | 00 00 …
```

The **KNOWN-BYTE @rec+0x06** is the per-cluster "discovered/identified" flag:

| save | # of 31 records with KNOWN-BYTE != 0 | which one |
|---|---|---|
| Save12 (0/2 baseline) | **0** | — |
| Save14 (2/2 REAL scan) | **1** | cluster `0x0083CE63` → `e1 3f 01 00 00 00 `**`01`**` 01 00 00 00 83 ce 63 00` @R1+0x57D71 |
| Save19 (MOD write) | **0** | cluster `0x0083CE71`'s byte is **0** @R1+0x577EE — never set |

The real scan flips **exactly one** byte 0→1 (the scanned target's cluster); the mod's ref-free
`CompleteTraitSlot` leaves all 31 at 0. (`clusterId` is a transient per-instance biome-cluster id —
`0x0083CE63` in session 43, `0x0083CE71` in session 45 — not a stable FormID, so compare by
"how many bytes != 0", not by id.) Reproduce: see the `0x00013FE1`-walk below.

### WHAT THIS STORE IS (decompile-verified)

This is the **known-set / identity-reveal store** at **`*(ID_937609 + 0x160)`** (the live biome's
discovered-set), written by **`ID_83025(knownSet, ref, canonId)`** → `ID_83024` (the catalogue
populator). It is the SAME store `trait-onplanet-completion-2026-06-23.md` §2 step D calls
"Unknown→named". A real scan reaches it via `ID_90506`→`ID_52157`→`ID_52158`→`ID_83025`. It IS
serialized into GlobalData R1 (this diff proves it persists) under discriminator `0x00013FE1`.

### THE RENDER GATE (decompile-verified — `ID_90518` panel painter, case 3/4)

`onp-resolver-2026-06-23.txt:679-731` — the scanner panel name-reveal:

```c
local_600 = (durablePercent == 100);                       // 938333 pct == 100 (BOTH saves pass)
if (local_600) {
    knownSet = *(ID_937609 + 0x160);
    ID_56887(knownSet+0x20, &slot, &canonId);              // FNV-find canonical in the KNOWN-SET
    if (slot is the end sentinel)      reveal = false;     // canonical NOT in known-set  → "UNKNOWN FEATURE"
    else if (slot entry is empty)      ID_83025(knownSet, ref, canonId);  // (live populate, on-planet only)
    else                               reveal = real name; // canonical IS known            → "MICROBIAL COMMUNITY"
}
```

So the named/complete render requires **BOTH** (a) `938333` pct == 100 (the slot the mod already
writes) **AND** (b) the canonical id present in the **known-set** (`ID_937609+0x160`, disc
`0x00013FE1`) — which the mod does NOT write in its ref-free path. `local_600`'s percent gate is why
the slot still matters, but the slot alone is insufficient; the known-set membership is the missing
half. (`ID_52159`@1407b8750 — the OTHER reveal reader, used by `ID_90491` — keys only `938333`
slot+0x21 and returns 2 for BOTH saves, confirming `938333` is not the discriminating input.)

### THE FIX (mod change)

`TestTraitRegistryWalk`'s ref-free path (`Engine::CompleteTraitSlot`, `src/Main.cpp:708`) writes the
`938333` slot + pooled keyword but **never the known-set**. To match Save14's reload render the
completion path must ALSO mark the canonical "known" in `ID_937609+0x160`:

- **On-planet (works today):** `CompleteScanTargetCredit`/`CompleteTraitScanTargetRef` already call
  `RevealKnownNative` = **`ID_83025(*(937609+0x160), ref, ID_83009(ref))`** on a LOADED ref. Route
  trait completion through that (per-loaded-ref) path, not the ref-free `CompleteTraitSlot`, so the
  known-set byte is set and persists → reloads as named/complete like Save14.
- **Ref-free (no loaded ref):** `ID_83025` needs `*(937609+0x160)` (the live biome) and a ref, so it
  is on-planet-bound. A purely ref-free pre-write would have to hand-build the `0x00013FE1` known-set
  record (`ID_56887`-insert the canonical id into `knownSet+0x20`, set rec+0x06=1) — allocator/
  discriminator-sensitive; prototype behind the latch. The honest minimal change is: **add the
  `ID_83025` known-set write (canonical = `ID_83009(ref)` = the scan-target base ACTI) to the
  scan-target completion path** so Save19's durable state gains the one byte Save14 has.

`[save-verified]` byte diff of Save12/14/19; `[decompile-verified]` `ID_90518` case 3/4 gate
(`onp-resolver:679-731`), `ID_83025`/`ID_83024` (`onp-resolver:2704-2733`), `ID_52159`
(`slot-0x08-readers-raw.txt:1260`). Reproduce: the `0x00013FE1`-walk in `compare_save19.py` context.

---

> **★★★ SECOND CORRECTION (2026-06-23 PM, supersedes the "+0x08 member array" answer below).**
> A full structural parse of the `938333` per-planet subobject across **Save12 (0/2) / Save13 (1/2) /
> Save14 (2/2 real) / Save18 (mod write)** — plus a 2-slot record from a progressed save — proves the
> subobject has **TWO** member arrays, and a real trait scan writes the keyword to the **POOLED** one,
> NOT the canonical slot's inline `+0x08`. The earlier "drive `ID_52158`" / "PushSpeciesAttr to
> `slotAddr+0x08`" recipe put the keyword in the WRONG array and is withdrawn. The exact layout, the
> three byte-level diffs, and the corrected `CompleteTraitSlot` are in the **NEW TOP SECTION** below;
> the older sections are retained for history but their `+0x08`/flag conclusions are superseded.
>
> Reproduce: `python re/save/compare_save18.py` + the structural parse in this section.

---

## ★★★ THE BYTE-EXACT STRUCTURE (verified Save12/13/14 vs Save18, the authoritative model)

The per-planet `938333` subobject record (planet Jemison, key `0x0043F5A1 = planetId 0x0003F5A1 ^ 0x00400000`)
serializes as:

```
ver(u32 = 3) | u16(0)
POOLED member array   : count(u32) + count×id(u32)           ← in memory: subobj+0x08, a 16-byte BSTArray {data, size, cap}
SLOT hashmap          : count(u32) + pad(u32) + slots         ← in memory: subobj+0x18 (keys) / subobj+0x40 (data) / +0x48 (end)
   each SLOT          : id(u32) | pct(u8 @slot+0x20) | flag(u8 @slot+0x21) | inline 24-byte BSTArray {begin@+0x08,end@+0x10,cap@+0x18}
```

The four states, clean record bytes (stripped to the suffix `…a2 cd 90 50` = next subobject):

```
Save12 0/2 (10 B): 03 00 00 00 | 00 00 00 00(pooled.count=0) | 00 00(slots.count truncated=empty)
Save13 1/2 (32 B): 03 00 00 00 | 00 00 | 00 00 00 00(pooled=0) | 01 00 00 00 00 00 00 00(slots.count=1,pad) | 50 b2 61 00 01 64(slot canon,flag=1,pct=100) | 00*8(slot inline +0x08 EMPTY)
Save14 2/2 (36 B): 03 00 00 00 | 00 00 | 01 00 00 00 88 55 62 00(POOLED count=1, kwd 0x00625588) | 01 00 00 00 00 00 00 00(slots=1,pad) | 50 b2 61 00 02 64(canon,flag=2,pct=100) | 00*8(slot inline +0x08 EMPTY)
Save18 mod (36 B): 03 00 00 00 | 00 00 | 00 00 00 00(POOLED count=0 EMPTY) | 01 00 00 00 00 00 00 00(slots=1,pad) | 50 b2 61 00 0a 64(canon,flag=0x0a=10 WRONG,pct=100) | 01 00 00 00 00 00 00 00 88 55 62 00(slot inline +0x08 = [kwd] WRONG ARRAY)
```

### Where the keyword lives in a REAL scan, definitively
- **REAL (Save14):** trait keyword `0x00225588` is in the **POOLED array** (subobj+0x08, serialized FIRST,
  before the slots). The canonical slot's **inline +0x08 array is EMPTY** (`00*8`). Slot flag = **2**.
- **MOD (Save18):** POOLED array is **EMPTY**; the keyword is in the canonical **slot's inline +0x08**
  (serialized AFTER the slot scalars). Slot flag = **10** (`0x0a`).

### The THREE byte-level differences (all three rejected the panel reveal on reload)
1. **Wrong array.** Mod pushed the keyword to `slotAddr+0x08` (the slot's inline 24-byte vector) via
   `PushSpeciesAttr`. A real scan puts it in the **POOLED 16-byte BSTArray at `subobj+0x08`** (the first
   `ID_45726`-serialized field of the PlayerKnowledge payload). These serialize in different positions →
   not byte-identical, and the trait-name reveal reads the pooled array, so the mod's record names nothing.
2. **Wrong flag.** Real slot+0x21 = **2**. Mod = **10** = `8` (stamped by the earlier `ScanRefNative(ref,1,8,0)`
   engine path that creates/seeds the slot before `CompleteTraitSlot` runs) `+ 2` (the two
   `IncrementScanFlag(…,1)` saturating accumulates). The accumulate inherits the prior 8. (Note: the
   discovered-bit reader `ID_124788` = `*(u32*)(slot+0x20) >> 2 & 1` passes for BOTH 2 and 10, so the flag
   alone is not the panel reject — but byte-parity with Save14 needs exactly 2.)
3. **Slot inline +0x08 not empty.** Real leaves it `00*8`; the mod's wrong push left `[kwd]` there.

### In-memory offsets (derived from ID_124898/124899/124900 + the load serializer ID_45726/ID_51422)
`subobj` = `entry+0x20` (the mod's `ResolvePlanetSubobj` return). The PlayerKnowledge serializer
(`knowledge-api.txt:1196-1200`) walks the payload as: `lVar9+0x28` (ID_45726, the **pooled BSTArray**),
`lVar9+0x38` (ID_51422, the **hashmap**), `lVar9+0x80` (ID_51420). With `lVar9 = subobj − 0x20`
(because hashmap `lVar9+0x38 == subobj+0x18`, the base ID_124898 FNV-hashes), the pooled BSTArray sits at
**`subobj+0x08`** and is **16 bytes** (gap to the hashmap at `+0x18` = 0x10): `{u32* data@+0x08,
u32 size@+0x10, u32 cap@+0x14}`. The canonical slot's inline array (slot stride 0x30) is a separate
**24-byte** `{begin@+0x08, end@+0x10, cap@+0x18}` (ID_35755 grows it; `ID_124900`/`ID_83041` read it for
the SPECIES catalogue — but it is EMPTY for a trait, whose keyword is in the pooled array).

### The corrected `CompleteTraitSlot` (src/Main.cpp — builds a record byte-identical to Save14)
```cpp
// 0) ensure the slot exists (ID_124899 creates it; pct seeded)
SetPercentByte(subobj, canonicalActi, 100, 0);
const auto idx      = SpeciesSlotHash(subobj+0x18, &canonicalActi);
const auto slotAddr = *(u64*)(subobj+0x40) + idx*0x30;
// 1) flag = EXACTLY 2, pct = 100 — DIRECT absolute writes (not the saturating accumulate)
*(u8*)(slotAddr+0x20) = 100;
*(u8*)(slotAddr+0x21) = 2;
// 2) CLEAR the canonical slot's inline 24-byte +0x08 array (a real scan leaves it empty); free engine buf
EngineScalarFree(*(u32**)(slotAddr+0x08), end-begin);  *(u64*)(slotAddr+0x08)=*(u64*)(slotAddr+0x10)=*(u64*)(slotAddr+0x18)=0;
// 3) append the keyword to the POOLED 16-byte BSTArray at subobj+0x08 via the engine allocator (ID_35770)
buf = EngineScalarAlloc((oldSize+1)*4, 4);  copy old; buf[oldSize]=traitKeyword;
*(u32**)(subobj+0x08)=buf; *(u32*)(subobj+0x10)=oldSize+1; *(u32*)(subobj+0x14)=oldSize+1;  // {data,size,cap}
```
Push the **raw** keyword `0x00225588`; the StoredComponent serializer (`ID_45726`/`ID_52193`) applies the
`^0x00400000` DB tag on save → emits `0x00625588`, matching Save14. **Status: implemented + compiles;
the pooled-array append (16-byte BSTArray field write + engine alloc/free) is decompile-derived and
should be smoke-tested in-game (write → save → reload → confirm "MICROBIAL COMMUNITY" + "100% SCANNED"),
since the `_capacityAndFlags` semantics of the 16-byte BSTArray were inferred from `ID_45726`'s read,
not a live dump.** A `DumpSpeciesSlots`-style hexdump of `subobj[0x00..0x20]` after a REAL in-game scan
would confirm the exact pooled-array field encoding before trusting the write on a sweep.

---


**Question.** Find the DURABLE save record that persists a completed trait scan-target ("100% SCANNED"
+ named feature) across reload, by diffing three Starfield saves that differ by exactly ONE real
hand-scan each, and identify the precise field the mod's current ref-free write OMITS.

> **★ CORRECTION (supersedes the prior version of this file).** The prior conclusion — *"there is no
> durable, id-keyed counter; nothing durable drives the completed panel; `938333` has no consumer"* —
> is **WRONG**. The user reloaded the 2/2 save in-game: the target shows **"MICROBIAL COMMUNITY"
> (named) + "100% SCANNED"** — completion **persists across reload**. The durable `938333`
> PlayerKnowledge record found by the diff IS what re-renders that completed/named panel on load. The
> earlier file looked for the record in the **ChangeForms section / formID array** (where it is not)
> and never decoded it. It actually lives in **GlobalData region 1** (the BSComponentDB2 ModuleState
> region), and it is decoded byte-exact below.

**Dataset** (all "Jemison – Sentient Microbial Colony", seconds apart, only diff = one scan):

| label | save | body bytes | GlobalData R1 bytes | changeFormCount |
|---|---|---|---|---|
| 0/2 unscanned | Save12_…182623…sfs | 5,568,769 | **495,903** | 15362 |
| 1/2 one scanned | Save13_…182638…sfs | 5,574,778 | **495,925** (+22) | 15363 |
| 2/2 both scanned | Save14_…182648…sfs | 5,572,435 | **495,939** (+14) | 15363 |

Confidence: `[save-verified]` = byte-exact diff of these three real `.sfs`; `[decompile-verified]` =
from `re/ghidra/output/{scan-count-runtime-store,panel-count-source,slot-0x08-catalogue-writer,
trait-scan-target-durable-store}-2026-06-23.md` + `trait-durable-q1.txt`/`flag-setters.txt`.
Reproduce with `python re/save/decode_pk_record.py`.

---

## ★ ANSWER (one line)

A real scan writes the **`938333` PlayerKnowledge per-planet subobject** (in **GlobalData region 1**,
key `(938333<<48)|(planetId<<16)`). For each scanned canonical it stores a **slot** = `<canonicalId>
<flag=+0x21> <pct=+0x20>` **PLUS a member/catalogue array (= slot+0x08 `BSTArray<u32>`)**. The mod's
current ref-free write sets **only the flag (+0x21) and pct (+0x20)** (via `ID_124898`/`ID_124899`) and
leaves the **member array (+0x08) EMPTY**. The completed-panel reveal reader **`ID_124900` reads
slot+0x08** to splice in the named feature, so an empty +0x08 renders "UNKNOWN FEATURE" / incomplete on
reload even though +0x21/+0x20 persisted. **FIX: drive the engine's own member-populating writer
`ID_52158` (it fills slot+0x08), not the bare `ID_124898`/`ID_124899`.**

---

## ★ THE DECODED `938333` RECORD (byte-exact, `[save-verified]`)

Located in **GlobalData region 1** = `body[offsetA:offsetB]`. The per-planet PlayerKnowledge
subobject's scanned-sublist begins at **R1 + 0x46581** (just after a tagged-size dword
`0x8000003X`, top bit = "has data", low = byte length). It GROWS +22 B on scan#1 (CREATE) and edits
**in place** on scan#2 (no length change → the `changeFormCount` is unrelated runtime noise; the
durable delta is here).

### Raw, aligned (the scanned-sublist, ending at the stable suffix `03 00 00 00 00 00 00 00 a2 cd 90 50`):

```
0/2 unscanned : 03 00 00 00  00 00  00 00 00 00                                            (empty list, count 0)
1/2 scanned   : 03 00 00 00  00 00  00 00 00 00  01 00 00 00 00 00 00 00  50 b2 61 00 01 64  00 00 00 00 00 00 00 00
2/2 scanned   : 03 00 00 00  00 00  01 00 00 00  88 55 62 00 01 00 00 00 00 00 00 00  50 b2 61 00 02 64  00 00 00 00 00 00 00 00
```

### Field decode

| token | meaning |
|---|---|
| `03 00 00 00` | subobject header / version (constant) |
| `01 00 00 00` (member-count u32) | **MEMBER / catalogue array count** (= the `slot+0x08 BSTArray<u32>` length): **0 → 0 → 1** |
| `88 55 62 00` = `0x00625588` | member id = **trait KEYWORD `0x00225588`** (appended on scan#2) |
| `50 b2 61 00` = `0x0061B250` | per-canonical SLOT id = **base ACTI `0x0021B250`** (the scan-target canonical) |
| byte after slot id (`01`/`02`) | **slot+0x21 scan-flag** — saturating accumulate **1 → 2** across the two scans |
| next byte (`64`) | **slot+0x20 percent** = 0x64 = **100** |

### ID encoding (DB-local index, not raw FormID)

The ids are stored as **BSComponentDB2 DB-local component indices** = `FormID ^ 0x00400000` (bit 22
tagged). This is **why the prior FormID-array / raw-LE32 search found nothing** (`0x0021B250` /
`0x00225588` appear 0× in all three bodies). Undo the tag:

- `0x0061B250 ^ 0x00400000 = 0x0021B250` ✓ (base ACTI `PlanetTraitScanTarget20SentientMicrobialColonies`)
- `0x00625588 ^ 0x00400000 = 0x00225588` ✓ (the trait keyword)
- `planetId 0x0003F5A1` is NOT inlined in this record — it is the **record key** `(938333<<48)|
  (planetId<<16)`, resolved through the DB, so it appears only as the 62× starmap occurrences the prior
  pass saw (byte-identical, unrelated).

### What changed across the three states (the mechanism, byte-for-byte)

1. **scan#1 (0/2→1/2):** member-count stays 0; a SLOT for the canonical `0x0021B250` is created with
   **flag=1, pct=100**. (slot+0x08 member array is still empty.)
2. **scan#2 (1/2→2/2):** member-count **0→1**, member = the trait keyword `0x00225588` (flag 1);
   the canonical slot's **flag saturates 1→2** (matching `ID_124898`'s `bVar1 + delta` accumulate),
   pct stays 100. This is the `ID_52158` biome-cluster pass populating **slot+0x08** + the cascade
   that marks the trait-community group "discovered."

The member array (slot+0x08) is the field that becomes non-empty as the feature is fully catalogued —
exactly the field the prior writeup never saw because it never reached this record.

---

## ★ THE FIELD THE MOD OMITS (and the decompiled reason)

`[decompile-verified]` `flag-setters.txt` / `trait-durable-q1.txt` / `slot-0x08-catalogue-writer-2026-06-22.md`.

The per-(planet,canonical) survey slot (stride 0x30) has THREE relevant fields:

```
slot+0x08 (ptr×3)  BSTArray<u32> begin/end/cap  — "catalogued member" ids (trait keyword / co-biome)
slot+0x20 (u8)     PERCENT byte                 — written by ID_124899
slot+0x21 (u8)     SCAN-FLAG byte               — written by ID_124898 (saturating)
```

- The mod's ref-free path (`MarkSpeciesScannedForPlanet`, `src/Main.cpp:620`) calls
  **`IncrementScanFlag` = ID_124898** (writes `+0x21`) and **`SetPercentByte` = ID_124899** (writes
  `+0x20`). On a freshly-created slot, `ID_124898` moves a **NULL staging array** into `slot+0x08`
  (it never stages members), so **slot+0x08 is born empty**.
- A real scan reaches **`ID_52157 → ID_52158`**. `ID_52158` does the +0x21/+0x20 writes **and then**
  (its lines 408–543, the `ID_937609+0x160` biome-cluster pass via `ID_56887`/`ID_83025`/
  `ID_35755`/`ID_37867`) **pushes the catalogued member ids into `slot+0x08`** and cascades
  `ID_52157` to grouped members. That member push is exactly the `0x00225588` appearance decoded
  above.
- The completed-panel / "named feature" reveal is **case-5 of `ID_90518`**, which calls
  **`ID_124900(subobj, ID_83009(ref), out)`** → **`ID_37875`** to splice **slot+0x08** members into
  the detail panel (`panel-count-source-2026-06-23.md` §4/§6). With slot+0x08 empty the reveal returns
  nothing → title stays `$ScanMapMarker_Unscanned` ("UNKNOWN FEATURE"); with it populated (real scan,
  or `ID_52158`) the feature is named and the panel reads complete.
- The whole subobject — **including each slot's `+0x08` member array, `+0x20`, `+0x21`** — is
  serialized (`ID_52193`/`ID_45726`) and restored (`ID_51523`/`ID_124656`) under the `938333` key,
  so what we decoded is exactly what round-trips through reload.

**So the omitted field is `slot+0x08` (the member/catalogue `BSTArray<u32>`).** Writing only +0x21/+0x20
gives "survey-% credit but no named/complete panel" — and that empty-+0x08 state is what the user saw
persist as "still 0/2 Unknown" after the mod-write→save→reload test.

---

## ★ THE EXACT RUNTIME WRITE RECIPE THE MOD MUST USE

To make a ref-free write byte-equivalent to a real scan's `938333` record (persists + drives the
completed/named panel on materialize):

1. **Do NOT use the bare `ID_124898`/`ID_124899` pair for the trait scan-target.** They set +0x21/+0x20
   only and leave `slot+0x08` empty.
2. **Drive `ID_52158` (PlanetProgressInner) with an explicit planet** — the mod ALREADY has this as
   `Engine::CompleteTypeForPlanet(planetId, canonicalId, ref)` (`src/Main.cpp:671`, ctx layout
   `{u32 planetId@0x00, u32 species@0x04, u8 delta=1@0x08, void* ref@0x10, u8* outScanned@0x18,
   u8* outPercent@0x20, u8 flag28=0@0x28}`, `PlanetProgressInner(&ctx,&dbPtr)`). `ID_52158` writes
   +0x21/+0x20 **and** populates `slot+0x08` (the member array) + cascades the group reveal.
   - **Key / args:** `planetId` = the target planet PNDT FormID (== `ID_52188`'s id == `+0x54`);
     `species`/canonical = the scan-target's **base ACTI FormID `0x0021B250`** (ESM-derivable, =
     `ID_83009` for that ref). The slot is keyed by FNV-1a of that canonical id in `subobj(entry+0x20)
     +0x18`; the saved index is `canonicalId ^ 0x00400000`.
   - **Caveat (decompile-verified, unchanged):** `ID_52158`'s slot+0x08 pass reads the **current live
     biome** (`ID_937609+0x160`), so the member array is populated from the materialized biome — i.e.
     this is robust **on-planet** (the working `CompleteSurvey` already triggers it). A purely
     ref-free/off-planet `ID_52158` will write +0x21/+0x20 and the planet key correctly but may leave
     slot+0x08 thin if the biome isn't loaded. If a fully ref-free slot+0x08 is required, hand-build a
     `BSTArray<u32>` of the authored member ids (the trait keyword `0x00225588` + co-biome species)
     with the engine allocator and stage it at `subobj+0x08..0x18` **before** the `ID_124898` create
     so the create-move carries it into the new slot — allocator/ownership-sensitive, prototype behind
     the guarded latch first (`slot-0x08-catalogue-writer-2026-06-22.md` §5).
3. **Title-reveal also needs the durable `938333` discovered state**, which `ID_52157`/`ID_52158`
   writes (the member push + the per-canonical flag). The literal on-screen "N/M" digits additionally
   re-render only when the scanner refresh runs (`ID_90530`/`ID_90517→ID_90548`,
   `panel-count-source-2026-06-23.md` §5) — but the **persisted completed/named panel on reload** is
   driven by the `938333` member array, which `ID_52158` fills.

**Minimal change:** for trait scan-target completion, replace the `MarkSpeciesScannedForPlanet`
(+0x21/+0x20-only) write with `CompleteTypeForPlanet` (= `ID_52158`) keyed by `(planetId, base-ACTI
canonical 0x0021B250)`, so the saved `938333` record carries the member array and reloads as
"named + 100% SCANNED" like a real scan.

---

## ★★ THE EXACT `+0x08` WRITE RECIPE (decompiled 2026-06-23, ref-free, on-planet not required for the array)

The mod ALREADY ships every primitive needed — `src/Main.cpp` `ResolvePlanetSubobj`, `SpeciesSlotHash`
(=`ID_124901`), `PushSpeciesAttr` (engine-allocator-safe `BSTArray<u32>::push_back` via `ID_35755`),
and `IncrementScanFlag`/`SetPercentByte`. The trait fix is a **direct hand-append of the trait keyword
to the canonical slot's `+0x08`**, mirroring exactly what `ID_52158`'s biome pass does — no need to
route through `ID_52158`'s biome-materialization-bound path.

### What `ID_52158` actually pushes (so we reproduce it byte-for-byte)

`ID_52158` @1407b81c0 lines 408–543 (onp-helpers-2026-06-23.txt): it reads the LIVE biome member
(`*(ID_937609+0x160)`), FNV-finds the species (`ID_56887`), collects the member's `uint[]` marker
ids `[member+0x08 .. member+0x10)`, then for each id appends it to the **canonical slot's** own
`{begin@+0x08, end@+0x10, cap@+0x18}` array:
```c
lVar6   = *(u64*)(slotsBase);                 // = subobj+0x40 slots base
lVar20  = lVar6 + 8 + idx*0x30;               // = &slot+0x08  (the array HEADER)
puVar19 = *(u32**)(slot+0x10);                // end
if (puVar19 == *(u32**)(slot+0x18))           // end==cap → full → grow
     ID_35755(lVar20, puVar19, &member_id);   // BSTArray<u32> grow+insert (engine alloc ID_35770)
else { *puVar19 = member_id; *(u64*)(slot+0x10) += 4; }  // spare cap → in-place append
```
This is **identical** to the mod's `PushSpeciesAttr(slotAddr, id)` (`src/Main.cpp:261`). The pushed
value `member_id` is `*(u32*)(member+0x28)` = a **RAW FormID** (NOT the `^0x00400000` DB-index).

### Member-id value to push = the RAW trait keyword `0x00225588`  `[decompile+save-verified]`

The `^0x00400000` bit-22 tag the save bytes show (`0x00625588`) is applied **only at serialize time**
by the BSComponentDB2 StoredComponent serializer (`ID_52193`→`ID_45726`/`ID_51393`,
pk-save-serialize-2026-06-23.txt); the LIVE runtime array holds the untagged FormID. `ID_52158` pushes
the raw `*(member+0x28)` and `ID_52193` tags it on the way to disk. So **push the raw keyword
`0x00225588`**, and the engine's own serializer emits `0x00625588` on save — matching Save14 exactly.
(Pushing the pre-tagged `0x00625588` would double-tag to `0x00A25588` on disk → wrong.)

### The recipe (ref-free; the array does NOT need the live biome)

```cpp
// Make a trait scan-target's (planet, base-ACTI) 938333 slot byte-equivalent to a real scan's.
// canonicalActi = the scan-target base ACTI (e.g. 0x0021B250); traitKeyword = e.g. 0x00225588.
bool CompleteTraitSlot(std::uint32_t planetId, std::uint32_t canonicalActi, std::uint32_t traitKeyword)
{
    const auto db = GetKnowledgeDB();
    auto subobj   = ResolvePlanetSubobj(db, planetId);   // existing helper (out[2]+offset, +0x20)
    if (!subobj) return false;                            // planet entry must exist (data sweep / discover)
    const auto base = reinterpret_cast<std::uintptr_t>(subobj);

    // 1) Flag + percent on the SAME slot the engine keys (canonical ACTI), saturating to 2 (see below).
    IncrementScanFlag(subobj, canonicalActi, /*delta*/ 1, 0);          // ID_124898 -> slot+0x21
    IncrementScanFlag(subobj, canonicalActi, /*delta*/ 1, 0);          // call TWICE -> +0x21 == 2
    SetPercentByte   (subobj, canonicalActi, /*pct*/  100, 0);         // ID_124899 -> slot+0x20 == 0x64

    // 2) Resolve the slot address (same hash the engine uses) and append the keyword to +0x08.
    const auto hashmap = base + 0x18;                                  // species hashmap
    const auto hashEnd = *reinterpret_cast<std::uint64_t*>(base + 0x48);
    const auto slots   = *reinterpret_cast<std::uintptr_t*>(base + 0x40);
    const auto idx     = SpeciesSlotHash(hashmap, &canonicalActi);     // ID_124901 FNV-1a
    if (idx == hashEnd || !slots) return false;
    const auto slotAddr = slots + idx * 0x30;

    // Idempotency: only push if the keyword isn't already a member (avoid dup on re-run).
    auto* b = *reinterpret_cast<std::uint32_t**>(slotAddr + 0x08);
    auto* e = *reinterpret_cast<std::uint32_t**>(slotAddr + 0x10);
    for (auto* p = b; p && p != e; ++p) if (*p == traitKeyword) return true;

    PushSpeciesAttr(slotAddr, traitKeyword);    // engine-allocator BSTArray<u32> append (ID_35755)
    return true;
}
```

**Why this is allocator-safe:** `PushSpeciesAttr`→`ID_35755`→`ID_35770(n*4,4)` allocates from the
SAME engine heap the engine frees on slot teardown/rehash (`ID_35771`, 4-byte stride) and that
`ID_124837`/`ID_124839` move by value on rehash. So the array is engine-owned and round-trips through
save (`ID_52193`) and load (`ID_51523`) with no manual lifetime management. (This is already battle-
tested for SPECIES via `TestBuildArray`, `src/Main.cpp:1601`.)

### THE FLAG (=2): why mod got 8, why real is 2, how to set 2

- The "flag came out **8**" the prompt cites is NOT `slot+0x21`. It is the **subobj-level dirty bit**:
  `ID_124898`'s last line is `*param_1 = *param_1 | 8` (onp-helpers-2026-06-23.txt:618) — it ORs `8`
  into `subobj+0x00` (the attribute bitmask / dirty flag), every call. The per-canonical `slot+0x21`
  is a SEPARATE byte. (If a dump read `slot+0x21`==8 it would mean delta-8 was passed; the mod passes
  delta=100→`0x64`. The real scan's `+0x21`==**2**.)
- **`slot+0x21` is a saturating ACCUMULATOR** (`ID_124898`: on an EXISTING slot,
  `byte = min(0xFF, byte + delta)`; on a FRESH slot it STAMPS `delta` via the create-move at
  onp-helpers:595). A real scan calls `ID_52157→ID_52158→ID_124898(sub, canonical, delta=1)` **once
  per scan**; the scan-target needs **2** scans (M=2), so `+0x21` accumulates `1→2`. (`ID_52158`'s
  percent math then derives `+0x20` = `round(scanned/denom*100)`; denom for an ACTI canonical via
  `ID_47400=='ACTI'`→default `1`, so `+0x20`=100=`0x64`.)
- **The mod's `MarkSpeciesScannedForPlanet(..., delta=100)` stamps `+0x21`=`0x64`** (fine for survey-%,
  which only needs nonzero past threshold — but NOT byte-equal to a real scan). **To get exactly 2**:
  on a fresh slot, call `IncrementScanFlag(subobj, canonical, 1, 0)` **twice** (stamps 1, then
  accumulates 1→2), or once with `delta=2`. The percent byte is set independently to 100 by
  `SetPercentByte`. (For mere completion/persistence any nonzero `+0x21` ≥ denom works; `==2` only
  matters for byte-exact save parity with Save14.)

### ★ RESIDUAL RISK / SCOPE — read before trusting this for the VISIBLE panel

`[decompile-verified]` This recipe makes the saved `938333` record **byte-equal to Save14** and is read
back on reload by `ID_83041`/`ID_124900`→`ID_37875` (the member-array splice, slot-0x08-readers:1) —
so the **info-panel "named feature" reveal and the survey-% aggregator (`ID_97851`) ARE driven by it**.
HOWEVER, two independent docs decompiled the *scan-target outline + "N/M SCANNED" counter* and found
they read a **DIFFERENT, transient store**, NOT `938333`:
- The surface scan-target **outline color** = `ID_83007`→ per-ref `939118 +0x28` (transient, empty
  save stub, reset-to-0 on materialize) — `trait-scan-target-durable-store-2026-06-23.md` §★★.
- The on-screen **"N/M SCANNED" digits** = `ID_90518` copying `*(u8*)(ID_938422+0x38)`, a scanner-menu
  tally rebuilt ONLY by the engine's scan/re-aim refresh pass — `panel-count-source-2026-06-23.md` §2/§4.

So writing `+0x08`+flag ref-free reproduces a real scan's **durable PlayerKnowledge record** (persists,
names the feature in the survey/data panel, credits survey-%) but does **NOT** by itself green the
surface object's outline or move the live "N/M" digits — those remain materialization+aim-bound (only
the engine's on-planet scan input, or an on-aimed-ref `ID_90506`/`SetScanned`+refresh, drives them).
The earlier "ID_52158 reads the live biome so +0x08 is on-planet-bound" caveat applies ONLY to routing
through `ID_52158`'s biome pass; the **direct `PushSpeciesAttr` recipe above is fully ref-free** (it
hand-supplies the keyword, never touching the live biome) and so works off-planet for the durable
record. On-planet is required only if you ALSO want the live outline/counter to update in the same
session.

---

## ★ SAVE-SIDE FACTS (what was right vs corrected)

- **STILL TRUE:** the formID array is byte-identical across all three saves; the scan-target REFRs /
  base ACTI / trait keyword are absent as raw LE32; there is no per-REFR ChangeForm. `[save-verified]`
  (The keyword/canonical ARE present — but **only inside GlobalData R1, as `^0x00400000` DB indices**,
  not as raw FormIDs, which is why the raw search missed them.)
- **CORRECTED:** the durable scan delta is **not** "one opaque ChangeForm with no consumer." It is the
  **`938333` PlayerKnowledge per-planet subobject in GlobalData region 1**, decoded above, and it **is**
  read by `ID_124900` to render the persisted completed/named panel. The prior "no durable counter /
  nothing reads 938333 / count is transient-only" verdict is withdrawn.
- The transient `939118 +0x28` byte (outline color + live "N/M" tally via `ID_90522`/`ID_938422+0x38`)
  is still a SEPARATE, transient domain — it is what greens the surface object in real time and is NOT
  serialized. The DURABLE completed/named panel is the `938333` member array. (Two domains, as in the
  species model.)

---

## Deliverable scripts (read-only, `re/save/`)

- **`decode_pk_record.py`** — NEW. Diffs GlobalData region 1, isolates the grown `938333` record,
  decodes the per-canonical slot (`<id><flag=+0x21><pct=+0x20>`) and the member array (slot+0x08),
  undoes the `^0x00400000` DB-index tag → ACTI `0x0021B250` + KYWD `0x00225588`. One-shot reproduction
  of everything above.
- `find_pk_record.py` — NEW. Proves the canonical/keyword/species/REFR FormIDs are absent as raw LE32
  in every body (the reason the prior pass missed the record), and that planetId 0x0003F5A1 is 62×
  identical starmap data.
- `walk_changeforms.py` — NEW. ChangeForm-record walker probe (shows the durable delta is NOT in the
  ChangeForms section — it is in GlobalData R1).
- Retained from prior pass: `analyze_scan_count.py`, `diff_changeforms.py`, `diff_real_fidarray.py`,
  `diff_all_regions.py`, `decode_records.py`, `diff_sections.py`, `locate_insert.py` (their formID-array
  / ChangeForm findings stand; their *conclusion* about the durable store is corrected here).

Cross-refs: `re/ghidra/output/panel-count-source-2026-06-23.md` (ID_124900 reveal reads slot+0x08;
ID_52157/52158 are the real write), `re/ghidra/output/slot-0x08-catalogue-writer-2026-06-22.md`
(slot+0x08 = BSTArray<u32>; ID_52158 the only writer; ID_124898 leaves it NULL),
`re/ghidra/output/trait-durable-q1.txt` (ID_52158 / ID_124898 / ID_124899 bodies),
`re/ghidra/output/scan-count-runtime-store-2026-06-23.md` (serialize ID_52193 / deserialize ID_51523 —
the subobject incl. each slot's +0x08 round-trips), `src/Main.cpp:620` (MarkSpeciesScannedForPlanet =
the +0x21/+0x20-only write that omits +0x08) and `:671` (CompleteTypeForPlanet = the ID_52158 fix path).
