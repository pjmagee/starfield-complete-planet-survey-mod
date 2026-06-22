# Overnight deep RE — 2026-06-22 (four-question canonical/green/event/arity probe)

Fresh decompile evidence against the 1.16.236 Ghidra project (offsets CONFIRMED identical
on 1.16.244 per `offset-skew-236-vs-244.md`, so the bodies below are byte-valid on the
shipped build). Tooling: `analyzeHeadless.bat` with `JAVA_HOME` set to the Microsoft JDK 21
(the batch fails silently — "system cannot find the file specified" — if `JAVA_HOME` is
unset; that was the only environment gotcha). New helper scripts added:
`ResolveVtableSlot.java`, `ResolveVtableAddr.java`, `DumpVtable.java`, `FindSymbols.java`.

New dumps this session:
- `q1-83004-asm.txt` — `ID_83004` asm (located the StoredComponent vftable `LEA [0x144c49f10]`)
- `q1-vtable-layout.txt` — 140 slots of the `0x144c49f10` vtable block
- `q1q2q3-batch.txt` — `ID_44957 47225 83124 83125 83121 83100-83105 52181 47749 64213 64214 101322 123824 52209 97853`
- `q1-scannable-cluster.txt` — `ID_83022 83026 83083 83087 83088 83089 47401 83046 83050`
- `q2-membership-key.txt` — `ID_52159 38719 90491`
- `q3-eventsource-xrefs.txt` — XrefsToId on `ID_838693 838697 945584 945583 945919 945920`
- `q4-arity-asm.txt` — raw asm of `ID_83009` + `ID_83038` at the `ID_126806` call sites

---

## Q1 — Is the green CANONICAL id STATIC (off-planet derivable) or live/materialization-dependent?

### VERDICT: **STATIC. The canonical resolver dereferences ONLY static base-FORM record fields. It is off-planet derivable from the ESM form with no live instance, world, or cell state.**

### Evidence — the resolver chain reads only form-record fields

`ID_83006` (`@141307670`, the form-level canonical resolver) has exactly two outcomes, and
**both read only the base form's own static component tree**:

```c
undefined8 ID_83006(longlong base) {
  uVar2 = ID_63393();                 // default/fallback path (see below)
  if (ID_64338(base) != 0) {
    plVar3 = ID_44958(*(base+0xc8), 0);   // base+0xc8 = the form's STATIC component container
    if (plVar3 != 0)
      return (*(plVar3+0x428))(plVar3); // primary canonical getter (vtable call on a component)
  }
  return uVar2;
}
```

Every field access in the entire chain is on the **base form record** (the thing
`LookupFormByID`/`ID_47401` returns), never on a `TESObjectREFR` instance, cell, or world:

- **Gate `ID_64338`** (`@140ba6320`, `canonical-chain-2026-06-21.txt:46-63`): reads
  `*(base+0x98)` and the byte `*( *(base+0x98) + 0x2e) == '2'`, then `ID_36022(base+0xc8,0x2b)`
  or `ID_47225(*(base+0x98)+0xe8, -1)`. `base+0x98` is a static sub-form pointer; `+0x2e` is a
  form-type tag byte; `base+0xc8` is the component bitfield container. **No instance/world deref.**
- **`ID_44958`** (`@14051b070`, `canonical-chain-2026-06-21.txt:1-42`): walks the linked list at
  `base+0x18`/`base+0x8`, finds the entry whose tag byte `+0x12 == '+'`, returns
  `*(entry + 0x20 + idx*8)`. Pure static component-list walk.
- **Primary getter `(*(plVar3+0x428))(plVar3)`**: `plVar3` is the component pulled from
  `base+0xc8`. Its peer accessors in the same StoredComponent vtable cluster
  (`ID_83046 @14130b080`, `q1-scannable-cluster.txt:305-323`) only marshal the component's own
  stored fields: `param_2[4] = *(comp+0x20)` (authored id), `param_2[5] = *(comp+0x28)`
  (canonical id). These ids are **baked into the component record**, not computed from live state.
- **Fallback `ID_63393`** (`@140b4d430`, `canonical-chain-2026-06-21.txt:67-162`): reads
  `*(base+0x98)`, walks a keyword/component list (`ID_47393` over the static `ID_937842` keyword
  registry) for a `+0x2e=='B'` entry, and resolves a form via `*(base+0x28)` → `ID_63938`. Can
  return `*(base+0x98)` (the base form itself). When `base+0x98==0` it calls…
- **`ID_44957`** (`@14051afd0`, NEW this session, `q1q2q3-batch.txt:1-42`): identical shape to
  `ID_44958` — walks `base+0x18`/`base+0x8` for the `'+'`-tagged entry, returns `*(entry+0x18)`.
  Pure static list walk.
- **`ID_47225`** (`@1405d37b0`, NEW, `q1q2q3-batch.txt:46-121`): reads form-flag fields
  `*(base+0x16)` (short), `*(base+0x38)`, `*(base+0x48)`, and `+0x2e` type tags, recursing into
  `+0xe8` sub-lists. All static form-record bitfields.

### Why the static vtable[0x428] could not be pinned to ONE concrete function (and why it does not matter)

`ID_83004` loads the StoredComponent<ScannableComponent> vftable as `LEA RAX,[0x144c49f10]`
(`q1-83004-asm.txt:56`). Dumping that address (`q1-vtable-layout.txt`) shows it is **not a flat
vtable** — it is a multiple-inheritance vtable *block* with interleaved
`RTTI_Complete_Object_Locator` pointers and embedded RTTI class-name strings ("ShipManagement",
"SpacesWeaponBinding", "SubSceneComponent", "VehicleManagement", "SurveyScanComponent…"). Slot
`+0x428` of THIS block lands on an RTTI locator (a sub-object boundary), not a function, because
the `vtable[0x428]` call in `ID_83006` is dispatched on `plVar3` (a component from `base+0xc8`)
whose concrete RTTI type is only known at runtime. The decompiler also flags it:
*"Could not recover jumptable… Treating indirect jump as call"* (`scan-inner.txt:77`).

**This is immaterial to the verdict.** The input to that call (`plVar3 = ID_44958(base+0xc8,0)`)
is derived purely from static form fields, and every candidate ScannableComponent-cluster
accessor reads only the component's own stored id fields. There is no path in `ID_83006`,
`ID_64338`, `ID_63393`, `ID_44957`, `ID_44958`, or `ID_47225` that reads a `TESObjectREFR`,
a cell, a world-space, or any per-load/per-instance state.

### Consequence

- **Remote pre-green is NOT blocked by canonical computation.** The canonical id is a pure
  function of the ESM base form, computable off-planet with no live instance.
- The G3c access violation was therefore **a call-shape bug, not proof of impossibility**: the
  mod's `CanonicalFormId` feeds `ID_83006` a raw form and the `vtable[0x428]` indirect call on a
  component that does not satisfy the gate's `base+0x98`/`+0x2e=='2'` shape faults. Feeding a form
  whose `base+0x98` is null makes `ID_64338` return false cleanly (no fault); feeding one whose
  `+0x98` is non-null but mis-shaped is what AV'd. The fix is to honor the gate (check
  `ID_64338`/null-guard `base+0x98`) before the call, **not** to abandon off-planet canonical.

---

## Q2 — Can the ID_52180 MEMBERSHIP branch color the outline independently of the +0x21 read?

### VERDICT: **YES. A species renders GREEN via membership ALONE — by ABSENCE from the `ID_52180` list — with no +0x21 write at all. +0x21 is NOT the sole lever.**

### Evidence — the OR in the renderer

`ID_90491` (`@141598460`, re-read fresh this session, `q2-membership-key.txt:151-290`):

```c
cVar4 = ID_52159(player, *(uint*)(param_1 + 0x28));   // read the +0x21 green byte
bVar12 = cVar4 != '\0';                                // TERM 1: green if +0x21 set
if (cVar4 == '\0') {                                   // only if +0x21 absent:
    ID_52180(iVar10, &local_48);                       //   build the membership id list
    ... for (piVar6 = local_38; piVar6 != piVar7 && *piVar6 != *(int*)(param_1+0x28); ++piVar6) ;
    bVar12 = (piVar6 == piVar7);                        // TERM 2: GREEN iff id ABSENT from list
}
return bVar12;
```

Final outline boolean = **`(+0x21 set) OR (rendered species id ABSENT from the ID_52180 list)`**.
Term 2 flips the outline green entirely on its own when the species is not in the list — no
`+0x21` byte is consulted on that branch.

### The membership list, its populator, and its KEY DOMAIN

- `ID_52180` (`@1407bbfe0`) is zero-arg `(void)` returning `ID_52181()` — **confirms the gap
  analysis ABI note (H-5)**; the prior KB `(iVar10,&local_48)` signature was wrong. The list
  pointer is threaded through `in_R9`.
- `ID_52181` (`@1407bc0b0`, NEW, `q1q2q3-batch.txt:330-531`) iterates DB tree nodes and, for each
  node, appends the species ids found at node offsets `+0x70, +0x74, +0x78, +0x7c, +0x80, +0x84,
  +0x88` into the int array (deduping). These are **raw 4-byte species FormIDs**.
- The list is sourced via `ID_47749` (`@1405f8630`, NEW, `q1q2q3-batch.txt:535-635`): it builds a
  DB key from discriminator **`ID_938158`** with `| 0x10000`, then looks up a tree at
  `*(planet + 0x300)` (`ID_38719`). So the membership set is **planet-scoped** (keyed off the
  player's resolved planet) and holds **authored species FormIDs**.
- Crucially, the value `ID_90491` compares against the list is `*(int*)(param_1 + 0x28)` — the
  **rendered form's own FormID field** — the *same* key `ID_52159` hashes for the +0x21 read.
  So both branches operate in the **same key domain** (the rendered-form id at `+0x28`).

### Consequence / reframing

- The "green vs blue" outcome is a **two-lever** decision. A species can be green because
  +0x21 is set OR because it is simply not a tracked member of the planet's `ID_938158` set.
- This explains spurious greens (a never-written species absent from the list reads green by
  Term 2) and is a candidate explanation for some "blue despite 100%" cases: if a species **is**
  present in the `ID_938158` membership set for the player's planet AND its +0x21 slot is 0
  (because the write landed under a different id / different planet), Term 2 evaluates to
  `present → NOT absent → false`, and the outline stays blue. The membership set, not just +0x21,
  is therefore part of the fix surface. **+0x21 is necessary for the "directly scanned" path but
  is not the only thing the renderer reads.**

---

## Q3 — Is PlayerKnowledgeFlagSetEvent load-bearing for PERSISTENCE (does a raw +0x21 write "rot on save" without it)?

### VERDICT: **NO. Firing the event is NOT required for a raw +0x21 to survive save/reload. The persistent state is the byte itself in the saved `db+0x268` PlayerKnowledge component; the events are UI/notification only and have no static save-committing subscriber.**

### Evidence

1. **Ground-truth anchor (decisive).** The mod's `MarkSpeciesScannedForPlanet`
   (`src/Main.cpp:289-306`) writes `+0x21`/`+0x20` RAW via `ID_124898`/`ID_124899` and **does NOT
   fire `ID_97853`/`ID_101322`/any event** — yet G1/G2 confirm the mod's data % and green
   PERSIST across quit-to-desktop. A raw byte write that skips the entire event layer already
   survives save/reload in practice. This alone settles the verdict; the decompiles below explain
   *why*.

2. **The events are notification dispatchers, not persistence writers.**
   - `ID_64213` (`@140b9c640`) / `ID_64214` (`@140b9c6b0`) are thread-safe singleton getters that
     just return the EventSource instance addresses `&ID_838693` / `&ID_838697`
     (`q1q2q3-batch.txt:639-671`).
   - `ID_52209` (`@1407c0880`) returns the `BSTGlobalEvent::EventSource<BGSPlanet::
     PlayerKnowledgeFlagSetEvent>` singleton `&ID_945584` (`q1q2q3-batch.txt:1329-1350`).
   - `ID_101322` (`@141962310`, NEW, `q1q2q3-batch.txt:675-799`) computes a float XP/notify
     magnitude and dispatches through the **player UI object** (`param_1+0x70`, vtable calls at
     `+0x8`/`+0x28`/`+0x230`) — it writes nothing to `db+0x268`.
   - `ID_123824` (`@1422cb8f0`, NEW) is a generic `BSTEventSource::Notify` over a red-black tree
     of sinks; it touches only the EventSource's own sink array, never the knowledge DB.
   - `ID_97853` (`@1417daca0`, NEW, `q1q2q3-batch.txt:1354-1410`) rebuilds the progress snapshot
     via `ID_1016657`/`ID_65318` and dispatches the per-species-progress event
     (`ID_64214()`-sourced) and, on planet-complete (`local_c8==local_c4`), the planet-complete
     event (`ID_64213()`-sourced). Both are pure `ID_123824` notifications (slate drop / XP / UI).

3. **No static save-committing subscriber exists.** `XrefsToId` on the three EventSource INSTANCE
   addresses (`q3-eventsource-xrefs.txt`):
   - `ID_838693 @145980a70`, `ID_838697 @145980a98`, `ID_945584 @146220530` — the **only**
     references to each are from their own getter functions (`ID_64213`/`ID_64214`/`ID_52209`).
   - There is **no `RegisterSink`/`AddEventSink` call site in the binary that takes any of these
     instance addresses**. (Runtime sinks register through the EventSource's own `this`-relative
     sink array, so even a dynamically-registered sink does not write the persistent
     `(planet,species)` slot — persistence is the slot, set by `ID_124898`, not the event.)

### Consequence

- A raw `+0x21` write is **self-sufficient for persistence**: the saved object is the
  `db+0x268` `938333` PlayerKnowledge entry (built by `ID_52204`, `knowledge-api.txt:1302-1348`),
  and `slot+0x21` is part of that saved record. `ID_124898` additionally sets `subobj+0x00 |= 8`
  (the dirty bit) on every call, so the engine marks the component for save without any event.
- What the mod's raw write genuinely SKIPS is **side effects, not persistence**: the Survey Data
  slate drop, survey XP, biome-group sibling recursion, current-biome propagation, and the
  "BIOME COMPLETE" UI transition. Those are cosmetic/economy, not the green-on-reload mechanism.

---

## Q4 — Does ID_126806 called 6/7-arg hit the SAME entry as the 3-arg body the mod uses?

### VERDICT: **SAME function, same entry, same 3-register ABI. The "6-arg / 7-arg" appearance is a decompiler artifact (spilled locals counted as parameters). There is no overload and no different table.**

### Evidence — raw asm at both canonical-read call sites (`q4-arity-asm.txt`)

`ID_83009` (`@1413079d0`) call site:
```
141307a5c  LEA  RCX,[RSI + 0x268]     ; arg1 = db+0x268 container
141307a63  LEA  R8,[RSP + 0x70]       ; arg3 = &key  (ID_938333<<48 | id<<16, built at a45-a57)
141307a68  LEA  RDX,[RSP + 0x30]      ; arg2 = &out
141307a6d  CALL 0x142412a30           ; -> ID_126806
```

`ID_83038` (`@14130a600`) call site:
```
14130a67b  LEA  RCX,[RBX + 0x268]     ; arg1 = db+0x268 container
14130a682  LEA  R8,[RSP + 0x80]       ; arg3 = &key
14130a68a  LEA  RDX,[RSP + 0x38]      ; arg2 = &out
14130a68f  CALL 0x142412a30           ; -> ID_126806
```

Both `CALL 0x142412a30`. `0x142412a30` is `ID_126806`'s entry (confirmed in
`offset-skew-236-vs-244.md` row 40: `126806 … 0x142412a30`, and a raw dump of that address shows
the standard prologue `48 89 5C 24 08 48 89 …` = `MOV [RSP+8],RBX; …`). The register setup at both
sites is the canonical Win64 3-arg call: `RCX = container`, `RDX = &out`, `R8 = &key`. The extra
"args" the decompiler attributed to `ID_83009`/`ID_83038` were stack spills (`RSP+0x70`, `RSP+0x30`
etc.), not real parameters.

### Consequence

The mod's 3-arg `DbLookup`/`ResolvePlanetSubobj` (`src/Main.cpp:251-270`) resolves the **identical
table** the engine's canonical-read sites use. There is no "wrong overload reads a different table"
failure mode (gap-analysis C-7 is **closed/disproven**). The "100% but blue" off-planet symptom is
NOT caused by a `ID_126806` arity/overload mismatch.

---

## BOTTOM LINE

**Persistent off-planet / remote green IS achievable, and is NOT blocked at the engine level.**
The three theoretical blockers are all cleared by this pass:

1. **Canonical id is STATIC** (Q1) — derivable off-planet from the ESM base form; no live instance
   or world state needed. The G3c crash was a call-shape bug (skipping the `ID_64338` gate /
   `base+0x98` null-guard), not an impossibility proof.
2. **Raw +0x21 persists without events** (Q3) — the saved record is the `db+0x268` `938333`
   PlayerKnowledge slot; `ID_124898` sets the dirty bit; no event subscriber commits the state.
   Confirmed by the mod's own raw writes already persisting (G1/G2).
3. **The lookup table is the same** (Q4) — no overload divergence; the mod hits the exact table
   the engine's canonical reads use. Offsets are identical on 1.16.244.

**The exact mechanism for persistent remote green:**
- Pre-create the `(938333 | planetId)` PlayerKnowledge entry ref-free for the target planet
  (`ID_52204`/`ID_102650`, already proven by the barren-planet data sweep, G4), then
- Write `+0x21` (and `+0x20`) under the species' **canonical id** = `ID_83006(base)+0x28`,
  computed off-planet via the gate-honoring path (check `ID_64338` / null-guard `base+0x98`
  before the `vtable[0x428]` call to avoid the G3c AV). For flora where authored == canonical
  (identity), the raw authored id already suffices.

**The one remaining caveat the renderer adds (Q2):** green is `(+0x21 set) OR (species ABSENT
from the planet's `ID_938158` membership set)`. The +0x21 write covers Term 1, so a correct
canonical +0x21 write greens regardless of membership. But the membership set is a *second*,
independent lever — relevant if a future approach tries to green by absence, and a possible
contributor to residual blue when a tracked species' +0x21 lands under the wrong key.

### IDs I could NOT fully resolve (and why it does not change the verdict)
- **The concrete `vtable[0x428]` function** — dispatched on a runtime-typed component
  (`ID_44958(base+0xc8,0)`); cannot be pinned to one address by static analysis (the
  `0x144c49f10` vtable is a multi-inheritance block whose `+0x428` slot is a sub-object/RTTI
  boundary). Immaterial: its INPUT and all peer accessors read only static base-form/component
  fields, which is what the Q1 verdict turns on.
- **Runtime sink registrants for the three EventSources** — none exist as static xrefs to the
  instance addresses; any runtime sinks register via `this`-relative arrays and still do not
  write the persistent slot. The Q3 verdict is anchored by the in-game ground truth that raw
  writes persist, so the absence of a visible subscriber only corroborates it.
