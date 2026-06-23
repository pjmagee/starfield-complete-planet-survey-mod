# Planet-id STAMP SITE — final decompile residual closed (2026-06-23)

**Question (the last RE residual):** Decompile the engine WRITE site that stamps the
planet id into the current-planet-id global `*(ID_937609 + 0x80)` (and/or the player
ExtraLocation(0x81) `node+0x28`), and determine whether the stamped value is the planet
form's FormID copied **verbatim** or a **separately-allocated** BSGalaxy NumericID.

## VERDICT — DEFINITIONAL IDENTITY (decompile-100%)

`Manager+0x80` (the value the renderer reads as the current planet id) is the planet
**FormID, copied byte-for-byte** from the setter's `int` argument, with **no converter and
no allocation**. A remote write keyed by an unvisited planet's FormID (== `planetForm+0x54`
== `planetForm+0x28`) is therefore *guaranteed* to be read by the renderer on arrival.
The earlier "separate BSGalaxy NumericID might be allocated" possibility is **ruled out**.

Confidence: very high. The stamp instruction, its argument provenance, and the
FormID-domain use of that same argument are all decompiled and cross-checked.

## The decompiled stamp site

`ID_937609` is the **`BGSPlanet::Manager` singleton** pointer (proved: its ctor
`ID_51725 @ 140788140` does `ID_937609 = param_1; *param_1 = BGSPlanet::Manager::vftable`,
and its dtor `ID_51729 @ 1407894d0` does `ID_937609 = 0`). `Manager+0x80` is a 32-bit
field = the current planet/body id (zeroed by ctor, read by every resolver).

**`ID_51735 @ 14078aa30` = `BGSPlanet::Manager::SetCurrentPlanet(this, int formId)`** is the
stamp site. Decompile (`re/ghidra/output/x80-real-setters.txt:437`):

```c
void ID_51735(longlong param_1 /*Manager this*/, int param_2 /*planet FormID*/) {
    *(int *)(param_1 + 0x80) = param_2;          // <-- STAMP, verbatim
    ...
    lVar12 = ID_124774(param_2);                 // LookupFormByID(param_2)  => param_2 is a FormID
    ...
    ID_51775(&db, param_2);                       // BSGalaxy DB key (OrbitState  : param_2)
    ID_51777(&db, param_2);                       // BSGalaxy DB key (BiomeData   : param_2)
}
```

Raw asm (`re/ghidra/output/setcurrentplanet-asm.txt`), the load-bearing lines:

```
14078aa30  MOV  dword ptr [RSP + 0x10], EDX      ; param_2 (formId) in EDX
14078aa4f  MOV  ESI, EDX
14078aa51  MOV  R13, RCX                          ; Manager this
14078aa54  MOV  dword ptr [RCX + 0x80], EDX       ; *** Manager+0x80 = param_2 VERBATIM ***
14078aa7e  MOV  ECX, EDX
14078aa80  CALL 0x142335670                       ; ID_124774 = LookupFormByID(param_2)
```

No arithmetic, no table indirection, no allocation between the argument and the store —
`mov [this+0x80], edx` where `edx` IS the unmodified `int formId` argument.

## Argument provenance (param_2 is the planet FormID, unchanged)

Caller **`ID_51734 @ 14078a5f0`** = `Manager::RequestSetCurrentPlanet(this, int param_2, ...)`
(`re/ghidra/output/setcurrentplanet-callers2.txt`). When the requested id differs from the
current `*(int*)(this+0x80)` (line 49 reads `+0x80` to compare), it forwards the **same
`param_2` verbatim** to the setter:

```c
if ((int)*plVar11 != param_2) {            // plVar11 = this+0x80
    if ((int)*plVar11 != 0) ID_51730(param_1, 1);   // reset previous
    bVar7 = ID_51735(param_1, param_2);              // SetCurrentPlanet(this, param_2)  <-- verbatim
    ...
}
```

`param_2` is treated as a FormID at every use:
- `ID_124774(param_2)` → `ID_124773 @ 1423355e0` builds key
  `((ID_937888 << 0x20) | (param_2 & 0xffffffff)) << 0x10`, runs `ID_126806`
  (BSComponentDB2 fetch), and returns a TESForm — i.e. **LookupFormByID over the FormID
  domain** (same terminal family as the resolver's `ID_51710 → ID_47401`).
- `ID_51775(&db, param_2)` / `ID_51777(&db, param_2)` build BSGalaxy component-DB keys
  `(componentTypeId , param_2)` — the **same key construction** the renderer uses when it
  reads rows (`ID_52189`, `ID_52158`, the body-row populator `ID_124595`). So the value in
  `Manager+0x80` is used *directly* as the BSGalaxy row key, with no remap.

## Cross-check against the read side (already established)

- `ID_52188 @ 1407bd600`: reads ExtraLocation(0x81) `node+0x28`; fallback `ID_56990`
  returns `*(int*)(ID_937609 + 0x80)`; both round-trip through `ID_51710 → ID_47401`
  (LookupFormByID, FormID domain). (`re/ghidra/output/planet-resolver.txt`,
  `resolve-planet.txt`, `form-lookup.txt`.)
- `ID_52189 @ 1407bd7f0:47`: packs `*(undefined4*)(ID_937609 + 0x80)` with a component-type
  id into the BSGalaxy lookup key — i.e. `+0x80` IS the per-body NumericID/FormID row key.
- The body-row populator `ID_124595 @ 1423204b0:475,634` writes `*(planetForm + 0x54) = 1`
  while keyed by the same `param_5` NumericID, tying `+0x54` and the `+0x80`/key together.

The setter side (`+0x80 = formId`) and the read side (`+0x80` used as FormID/DB key) are
the **same value in the same domain**. Hence `node+0x28 == Manager+0x80 == planetForm+0x54
== planetForm FormID(+0x28)` is an **identity stamped by `mov [this+0x80], edx`**, not a
converted/allocated id.

## Note on ID_937669 (false-anchor cleared)

`ID_937669` is NOT the planet id high-half — it is the **"BodyDraw" BSComponentDB2 component-
TYPE id**, registered in `BSGalaxy::ModuleState` ctor `ID_124590 @ 14231f630:141` via
`ID_126642("BodyDraw")` and torn down in dtor `ID_124591 @ 142320040:333`. In `ID_52189` it
is merely the *component-type* half of the `(type, bodyId)` key; the *bodyId* half is
`Manager+0x80`. This is why anchoring the search on `ID_937669` found only type-table
writers, not the planet stamp.

## Commands run (headless, Ghidra 12.0.4, JDK 21)

JAVA_HOME = `C:\Program Files\Microsoft\jdk-21.0.11.10-hotspot`
Tool = `C:/Tools/ghidra_12.0.4_PUBLIC/support/analyzeHeadless.bat`
Project = `D:/Projects/pjmagee/starfield-complete-planet-survey-mod/ghidra-project` (Starfield.exe 1.16.236; byte-identical to 1.16.244)

```
analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
  -scriptPath re/ghidra/scripts \
  -postScript XrefsToIds.java   stamp-xrefs-37878-937609.txt 37878 937609
  -postScript DecompileIds.java g80-writers.txt 51341 51725 51729 51788 99379
  -postScript DecompileIds.java g80-readers-near.txt 52189 52190 52158 52187
  -postScript FindFieldStores.java   manager-x80-stores.txt 937609 0x80
  -postScript FindStoreSig.java      x80-store-anchor937669.txt 0x80 937669
  -postScript XrefsToIds.java        xref-937669.txt 937669
  -postScript DecompileIds.java      planet-context-setters.txt 124590 124591 124595 124603
  -postScript FindStoresInRange.java x80-stores-bgsplanet-range.txt 0x80 0x140785000 0x1407d0000
  -postScript DecompileIds.java      x80-real-setters.txt 51730 51735      <-- STAMP found here
  -postScript XrefsToIds.java        x80-setter-callers.txt 51730 51735
  -postScript DecompileIds.java      setcurrentplanet-callers2.txt 51734 124774 51775 51777
  -postScript DecompileIds.java      formlookup-124773.txt 124773
  -postScript DumpAsm.java           setcurrentplanet-asm.txt 51735
```

New helper scripts added: `re/ghidra/scripts/FindFieldStores.java`,
`FindStoreSig.java`, `FindStoresInRange.java`.

## Implication for remote green

The stamp is `Manager+0x80 = planetForm FormID` (verbatim). Since the renderer's green
predicate keys off this FormID-domain id (`+0x80`/`node+0x28`), and the writer/survey path
uses the equal `planetForm+0x54`, a remote write under an **unvisited** planet's `+0x54`
FormID is **decompile-GO**: when the engine later lands/materializes that planet,
`SetCurrentPlanet` stamps exactly that FormID into `Manager+0x80`, and the renderer reads
the row the mod wrote. One save-test from definitional; no separate-id remap is required.
