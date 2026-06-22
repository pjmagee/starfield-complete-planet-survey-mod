# Offset skew analysis: 1.16.236 vs 1.16.244 (green/survey PlayerKnowledge DB)

Generated 2026-06-22 via fast-path RE (versionlib resolve + capstone disassembly of the
real 1.16.244 `Starfield.exe`), no full Ghidra re-import.
Tooling: `re/ghidra/scripts/offset_skew.py` (versionlib parser) + `offset_skew2.py` (diff).

---

## 1. VERDICT

**STABLE.** Every green/survey PlayerKnowledge-DB struct offset the mod hard-codes
(db+0x268, db+0x3d8 container, subobj +0x18/+0x40/+0x60, slot stride 0x30, percent byte
slot+0x20, scan-flag byte slot+0x21, bucket table `+0x12 + idx*4`, manager+0x8B0,
ScannableComponent +0x24/+0x28/+0xC8, vtable slots [0x228]/[0x428], key shift 0x10) is
**present and structurally identical** in the 1.16.244 function bodies. Only the function
*addresses* moved (whole-image shift, which SFSE already handles by resolving each ID
through the versionlib). **No struct-field offset moved.** A build-skew route to
"survey 100% but flora/fauna blue" is **ruled out** for these offsets.

---

## 2. Per-function address + offset table

`addr` column: every function relocated between builds (expected; the mod resolves by ID
via the versionlib, so this is benign). `offsets` column: result of disassembling the
1.16.244 body and confirming the mod's load-bearing displacements are present.

| ID | role | 236 addr | 244 addr | addr | load-bearing offsets (244) |
|----|------|----------|----------|------|----------------------------|
| 52159 | green reader / species-scanned check | 0x1407b8750 | 0x1407b7d60 | moved | 0x268, 0x12, 0x18, 0x20, 0x21, 0x30, 0x40, 0x48 — **MATCH** |
| 90491 | outline decider | 0x141598460 | 0x141597a50 | moved | manager+0x8B0 present (`mov rcx,[rax+0x8b0]`); db+0x268 reached via callee — **MATCH** |
| 90548 | outline decider 2 | 0x1415a47c0 | 0x1415a3db0 | moved | species instance slot +0x28 present; 0x268 via ID_52159/ID_52180 calls — **MATCH** |
| 52188 | planet-from-player | 0x1407bd600 | 0x1407bcbd0 | moved | no direct DB offset (resolver); body intact — **MATCH** |
| 52180 | membership | 0x1407bbfe0 | 0x1407bb5b0 | moved | db+0x268 present — **MATCH** |
| 52158 | write percent/scan (real scan chain) | 0x1407b81c0 | 0x1407b77d0 | moved | 0x268, 0x12, 0x18, 0x20, 0x21, 0x30, 0x40, 0x48 — **MATCH** |
| 52157 | scan chain entry | 0x1407b7fa0 | 0x1407b75b0 | moved | db+0x268 present — **MATCH** |
| 124898 | writer (scan flag +0x21) | 0x142348ad0 | 0x1423464e0 | moved | 0x21, 0x18, 0x30, 0x40, 0x48 — **MATCH** |
| 124899 | writer (percent byte +0x20) | 0x142348c20 | 0x142346630 | moved | 0x20, 0x21, 0x18, 0x40, 0x48 — **MATCH** |
| 124901 | FNV slot hash | 0x142348da0 | 0x1423467b0 | moved | 0x30 (stride/table), 0x28 (next ptr), 0x00 (key cmp, disp==0) — **MATCH** |
| 126806 | db lookup (bucket table) | 0x142412a30 | 0x1424105d0 | moved | `word ptr [base + idx*4 + 0x12]` + 0xfe0 sentinel — **MATCH** |
| 126578 | GetKnowledgeManager | 0x142401c50 | 0x1423ff640 | moved | Meyer's singleton; +0x8B0 is caller-side, not in body (by design) — **MATCH** |
| 83009 | ScannableComponent +0x24 canonical | 0x1413079d0 | 0x141307180 | moved | 0x24, 0x28, 0xc8, 0x268 — **MATCH** |
| 83004 | ScannableComponent lifecycle | 0x141307430 | 0x141306be0 | moved | 0x24, 0x28, 0xc8, vtable [0x228]+[0x428] — **MATCH** |
| 83006 | ScannableComponent resolve | 0x141307670 | 0x141306e20 | moved | 0x24, 0x28, vtable [0x428] — **MATCH** |
| 83038 | ScannableComponent canonical2 | 0x14130a600 | 0x141309db0 | moved | 0x24, 0x28, vtable [0x230] — **MATCH** |

### Key data-path offsets — direct disassembly confirmation on 244

ID_52158 (`real-scan-chain.txt` lines 169-184 in 236) reproduced 1:1 in the 244 body:

```
0x1407b7872  lea   rcx, [r12 + 0x18]          ; species hashmap = subobj+0x18  (-> ID_124901)
0x1407b787c  cmp   rax, [r12 + 0x48]          ; lVar16 != *(subobj+0x48)
0x1407b788b  add   rax, [r12 + 0x40]          ; slots base = subobj+0x40
0x1407b7890  movzx r15d, byte ptr [rax + 0x21]; scan-flag byte = slot+0x21
0x1407b7df7  lea   rcx, [rbp + 0x268]         ; db container = db+0x268       (-> ID_126806)
0x1407b7e52  movzx esi, byte ptr [rax+rcx*8+0x21]
```

ID_124899 (percent writer) on 244:

```
0x142346649  lea   rdi, [rcx + 0x18]              ; species hashmap subobj+0x18
0x14234665a  cmp   rax, [rbx + 0x48]              ; slots-end check subobj+0x48
0x14234666b  mov   byte ptr [rax+rcx*8+0x20], sil ; PERCENT byte = slot+0x20
0x1423466a5  mov   byte ptr [rdi + 0x20], sil     ; percent byte (insert path)
0x1423466a9  mov   byte ptr [rdi + 0x21], bl      ; SCAN-FLAG byte = slot+0x21
```

ID_126806 (bucket-table resolution) on 244:

```
0x1424105f6  mov   qword ptr [rdx + 0x18], 0xfe0       ; 0xfe0 sentinel (matches local_80==0xfe0)
0x142410637  movzx ecx, word ptr [rax + r8*4 + 0x12]   ; bucket = base + 0x12 + idx*4
```

---

## 3. Functions whose 244 body structurally differs from the 236 dump

**None.** Every function's 244 access pattern matches the 236 decompile (same struct
displacements, same call structure, only register allocation / instruction scheduling
differs — i.e. recompilation noise, not a layout change). The three IDs that initially
flagged as "missing" an offset in the automated pass were all confirmed benign:

- **ID_90491 / ID_90548 (outline deciders):** `db+0x268` is *not* dereferenced in their own
  bodies — they call `ID_52159` / `ID_52180` / `ID_126806`, which do. `90491` directly
  contains `mov rcx,[rax+0x8b0]` (manager→db). Expected delegation, matches 236.
- **ID_126578 (GetKnowledgeManager):** a thread-safe Meyer's singleton getter
  (`_Init_thread_header` + guard, returns `&ID_952732`). The `+0x8B0` (manager→db) lives
  in every *caller* (`knowledge-db.txt`, `green-render-path-2026-06-21.txt`,
  `canonical-chain-2026-06-21.txt`), not inside 126578. Matches 236 exactly.
- **ID_124901 (FNV slot hash):** the slot+0x00 key compare is a zero-displacement `[reg]`
  deref (disp==0, skipped by the displacement collector); stride 0x30 and next-ptr +0x28
  are both present. Matches 236.

---

## 4. Confidence + caveats

- **Parser verified.** The versionlib parser was validated against the canonical
  `offsets-1-16-236-0.txt` table: **0 mismatches across all 910,562 parsed IDs**, and all
  18 target IDs matched bit-for-bit. Both versionlibs are **format V5** (flat `uint32[id]`
  RVA array — `IMAGE_BASE 0x140000000`), not the older V2 delta-packed format; the parser
  handles both. 236 db: ver [1,16,236,0], 1,142,947 slots, 910,562 populated. 244 db:
  ver [1,16,244,0], 1,274,968 slots, 910,630 populated.
- **All 16 target IDs resolved** in the 244 versionlib (none missing). Each disassembled
  from the real 102 MB 244 `Starfield.exe` via pefile RVA→memory-mapped image + capstone
  (CS_ARCH_X86, CS_MODE_64), 3 KB window per function.
- **Method limitation:** this compares the 1.16.244 *disassembly* offsets against the
  1.16.236 *decompile* offsets (the 236 binary was not on disk, only its Ghidra dumps).
  This is a presence/structure check, not a byte-for-byte two-binary disassembly diff. It
  is sufficient to detect a moved struct field (the field would appear at a different
  displacement or vanish), which is the question asked. No field moved.
- **Already empirically validated on 244** (per project memory): the data-path offsets
  (db+0x268, +0x20/+0x21, 0x30 stride) — the mod's `%` reads 100% on the live 244 game.
  This analysis independently confirms the **render path** (ID_52159, ID_90491/90548,
  ID_83009/83004) and the **canonical +0x24 / vtable [0x228]/[0x428]** offsets are equally
  stable, closing the last unverified build-skew gap.

**Bottom line: the mod's hard-coded offsets are safe on 1.16.244. No code change required
for offset skew.**
