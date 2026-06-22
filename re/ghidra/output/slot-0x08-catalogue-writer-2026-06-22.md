# slot+0x08 BSTArray — the catalogue writer, element type, and ref-free verdict (2026-06-22)

**Question (from the in-game memory diff).** In the per-(planet,species) survey slot
(`subobj+0x40`, stride 0x30, FNV-keyed by species formId), a FULL scan populates a
`BSTArray {begin@+0x08, end@+0x10, cap@+0x18}` with heap pointers (~`0x1_6Exxxxxx`),
~0x10–0x14 bytes of payload, while the mod's `+0x21`-only poke (`ID_124898`) leaves it
NULL. That array is the "species catalogued/known" data the info panel
(Resource/Biomes/Genetics/Reproduction) + XP read. Find the engine writer, the element
type, and whether it can be driven ref-free.

New decompiles this pass (`re/ghidra/output/`):
`slot-0x08-helpers-raw.txt` (ID_37867, ID_35755, ID_37286, ID_124840, ID_56887, ID_83025,
ID_83052, ID_83059, ID_52172, ID_52174, ID_97851, ID_97850, ID_124900) and
`slot-0x08-readers-raw.txt` (ID_37875, ID_35757, ID_83024, ID_83053, ID_124837, ID_124839,
ID_90491, ID_90548, ID_52159).

---

## TL;DR (the four answers)

1. **WRITER of `slot+0x08`:** **`ID_52158`** — specifically its *biome-cluster pass*
   (real-scan-chain.txt:310–378, the `ID_56887`/`ID_83025`/`ID_35755`/`ID_37286` block),
   plus the slot-construction move in **`ID_124898`** (lines 596–601) / **`ID_124837`**
   (rehash) that *carries* the staged array into a freshly-created slot. The mod's
   `ID_124898` call writes `+0x21` but stages an **empty** `subobj+0x08..0x18` array, so
   the new slot's `+0x08` array is born NULL. **`ID_52158` is the only thing that fills it.**

2. **ELEMENT TYPE:** a `BSTArray<uint32_t>` — **4-byte species/sibling form-IDs** (the
   "co-located biome members" for this species), NOT pointers, NOT component structs. The
   heap pointer the diff saw at `slot+0x08` is the array's `begin` (one heap allocation of
   `uint32[]`); each element is `*(form+0x28)` = a form-ID. Confirmed by stride: `ID_35755`/
   `ID_37867` push with `>> 2` (4-byte) element math and `ID_124900` reads it as a `uint[]`.

3. **REF-FREE VERDICT: YES, partially — and it is NOT required for green.** The array's
   contents (sibling species form-IDs in the same biome) ARE statically derivable, BUT the
   engine collects them from the **live current biome** (`ID_937609+0x160`) via `ID_56887`,
   i.e. it needs the biome materialized — the same on-planet constraint as everything else.
   **Crucially, this array does NOT gate the green outline.** See answer 4.

4. **WHAT GATES GREEN:** `slot+0x21` **alone**. The render decider `ID_90491`/`ID_90548`
   calls `ID_52159(player, species)`, and `ID_52159` reads **only** `*(slot+0x21)` (raw,
   nonzero ⇒ green — readers-raw.txt:1309). It never touches `slot+0x08`. The
   `slot+0x21 == 0xC9/0xCA` (bit 0x80) vs our `0x64` difference is **immaterial to green**:
   both are nonzero, and both clear every survey-% threshold. **The `+0x08` array gates the
   INFO PANEL / XP, not the outline color.**

---

## 1. The two distinct slot structures (do not conflate)

There is exactly ONE survey slot struct (`subobj+0x40`, stride 0x30). Its full field map,
now complete:

```
slot+0x00 (u32)  species formId            (key; written by ID_124898/ID_124899/ID_124840)
slot+0x04 (f32)  a float                   (same half/full — set at slot creation)
slot+0x08 (ptr)  BSTArray<u32> begin   ─┐
slot+0x10 (ptr)  BSTArray<u32> end      ├─ "catalogued biome-member species" list
slot+0x18 (ptr)  BSTArray<u32> cap     ─┘   (written by ID_52158 biome pass; NULL on poke)
slot+0x20 (u8)   PERCENT byte              (ID_124899 write / ID_97851 threshold read)
slot+0x21 (u8)   SCAN-FLAG / GREEN byte    (ID_124898 saturating write / ID_52159 raw read)
slot+0x24 (u32)  survey-data dword         (ID_52174 write / ID_52172 read)
slot+0x28 (u32)  bucket next-link (hashmap chaining; +0x2c = self-index)
```

The same 0x10/0x18 pair appears at the **subobj level** (`subobj+0x08/+0x10/+0x18`) — that is
the **staging array** `ID_124898` moves into a new slot on creation. The in-game diff's
"subobj+0x08/+0x10/+0x18 also become a populated array" is that staging buffer being
non-empty during a real scan; it is transient scratch, copied into the slot.

---

## 2. The writer, traced literally

### 2.1 Slot creation moves a (possibly empty) array in — `ID_124898` / `ID_124837` / `ID_124839`

`ID_124898(subobj, species, delta, 0)` — when the slot is **absent**, it builds the slot via
`ID_124840` then (slot-0x08-helpers / real-scan-chain.txt:596–603):

```
uVar5 = *(u64*)(param_1 + 8);   // subobj+0x08  staging-array begin
*(u64*)(puVar8 + 2) = uVar5;    // slot+0x08
uVar4 = *(u64*)(param_1 + 10);  // subobj+0x10  end
*(u64*)(puVar8 + 4) = uVar4;    // slot+0x10
uVar3 = *(u64*)(param_1 + 0xc); // subobj+0x18  cap
*(u64*)(puVar8 + 6) = uVar3;    // slot+0x18
*(u8 *)(puVar8 + 8)        = (char)param_1[0xe];     // slot+0x20 percent (staged)
*(u8 *)((char*)puVar8+0x21)= *(u8*)((char*)param_1+0x39); // slot+0x21 from delta
```

`ID_124837` (rehash) and `ID_124839` (open-addressing relocate) move the identical
`{+0x08,+0x10,+0x18}` triple when slots are rehomed — confirming `+0x08..+0x18` is a real
owned BSTArray field of the slot, carried by value, freed via `ID_35771` (4-byte stride).

**The mod never stages `subobj+0x08`, so `param_1+8 == 0` and the slot's `+0x08` is born NULL.**
This is the engine-level reason the poke produces an empty `+0x08` array.

### 2.2 The real fill — `ID_52158` biome-cluster pass (the actual writer)

After `ID_124898`/`ID_124899`, `ID_52158` (real-scan-chain.txt:249–378) does:

1. `biome = *(ID_937609 + 0x160)` — the engine's **CURRENT biome** (NOT the write's target
   planet). `ID_56887(biome+0x20, &out, &species)` — FNV lookup (0x28-stride map) of this
   species in the current biome's member table. If short, `ID_83025` rematerializes the
   member list.
2. Reads the biome member's own `{begin@+0x08,end@+0x10}` (a `uint[]` at
   `*(biomeSlot)+8 .. +0x10`, `>> 2` element count) into local scratch `local_b0`.
3. Re-resolves the **survey slot** (`lVar16 = local_78` = `entry+0x20`,
   `ID_124901(subobj+0x18,&species)` → `lVar26 = idx*0x30`), then for each collected member
   id pushes it into **the slot's own `+0x08` array**:

```
// real-scan-chain.txt:342-352 — writing slot+0x08/+0x10/+0x18 directly
lVar6  = *(u64*)(lVar16 + 0x40);            // slots base = subobj+0x40
puVar19 = *(u32**)(lVar26 + 0x10 + lVar6);  // slot+0x10  (end)
if (puVar19 == *(u32**)(lVar26 + 0x18 + lVar6)) // == slot+0x18 (cap) → grow
     ID_35755(lVar20 /*slot+0x08 hdr*/, puVar19, &member_id);  // realloc + append
else { *puVar19 = member_id; *(u64*)(slot+0x10 hdr) += 4; }    // in-place append
```

`lVar20 = lVar6 + 8 + lVar26` = **`slot+0x08`** (the array header start). `ID_35755`
(slot-0x08-helpers-raw.txt:73) / `ID_37867` are the canonical `BSTArray<u32>::push_back`
grow-path (allocate `ID_35770`/`ID_37865`, copy, `puVar5[idx] = *param_3`, `>> 2` math).
`ID_37286` frees the local scratch (TLS deallocator). **This loop is what fills `slot+0x08`
with heap-allocated `uint32` ids.** The `~0x1_6Exxxxxx` pointer is that heap allocation.

### 2.3 The reader of `slot+0x08` — `ID_124900` (confirms element type + consumer)

`ID_124900(subobj, species, outBuf)` (slot-0x08-helpers-raw.txt:1217):

```
idx = ID_124901(subobj+0x18, &species);
if (idx != *(subobj+0x48))
    ID_37875(outBuf, *(outBuf+8),
             *(u64*)(*(subobj+0x40) + 8  + idx*0x30),   // slot+0x08  begin
             *(u64*)(*(subobj+0x40) + 0x10 + idx*0x30)); // slot+0x10  end
```

`ID_37875` is `BSTArray::insert(range)` over **4-byte elements** (`>> 2`). So `slot+0x08` is
read back as a `uint32[]` span and spliced into a caller buffer — this is the info-panel /
catalogue feed (the "Resource/Biomes/Genetics/Reproduction" + per-species known list).

---

## 3. Element type — definitive

**`BSTArray<uint32_t>` of co-biome species/sibling form-IDs.** Evidence:

- Every push/read uses 4-byte element arithmetic (`>> 2`, `+4`, `ID_35770(n*4,4)`).
- The source values are `*(uint*)(form+0x28)` = form-IDs, collected from the biome member
  table (`ID_83024`: `local_res8[0] = *(int*)(lVar4 + 0x28); ID_35755(param_3, …)` —
  slot-0x08-readers-raw.txt:206–214) and the sibling expanders `ID_83053`/`ID_83059`.
- NOT pointers to `ScannableComponent` instances and NOT per-ref records; the heap block is
  a single `uint32[]` allocation (begin..end ≈ 4–5 ids × 4 B ≈ the 0x10–0x14 bytes observed).

So the diff's "heap pointers ~0x1_6Exxxxxx" = the **one** `begin` pointer of this `uint32[]`;
the payload itself is plain form-IDs, not heap objects.

---

## 4. What gates GREEN vs the INFO PANEL (the decisive separation)

### Green outline — gated on `slot+0x21` ONLY
`ID_90491` (readers-raw.txt:566): `cVar4 = ID_52159(ID_922868 /*player*/, *(u32*)(form+0x28));
bVar12 = cVar4 != 0;`. `ID_52159` (readers-raw.txt:1301–1309) resolves the player's planet
(`ID_52188`), keys `(938333|planet)`, FNV-hashes the species, and returns **`*(slot+0x60base
+0x21+idx*0x30)`** — the raw `+0x21` byte. **No read of `+0x08`. No threshold.** `ID_90548`
mirrors it (readers-raw.txt:1093). Any nonzero `+0x21` ⇒ green. So `slot+0x21=0x64`
(our poke) greens *exactly as well as* `0xC9` for the outline — **provided it's keyed under the
species id the renderer hashes** (the canonical/ rendered-base id; the standing G3a key-domain
caveat is unchanged and is the real blue cause, not `+0x08`).

### `slot+0x21` value 0xC9/0xCA (bit 0x80) vs our 0x64 — meaning
`ID_124898` does a **saturating add** of `delta` into `+0x21`. A real scan accumulates
per-frame/per-sibling so the byte climbs past 0x80 (the 0x80 bit is just "≥128 accumulated",
a side effect of saturation, not a flag). Our single `delta=100` write lands `0x64`. **No
reader treats bit 0x80 specially:** `ID_52159` boolean-tests it; `ID_97851`
(slot-0x08-helpers-raw.txt:629–632) threshold-compares `ID_69506/69507 <= byte` — both
`0x64` and `0xC9` clear any sane threshold (the instant-scan GMSTs make it 1). **So bit 0x80
is NOT a gate; the green is not gated on it.**

### Info panel / Resource-Biomes-Genetics-Reproduction / XP — fed by `slot+0x08`
The per-species catalogue array (read by `ID_124900` → `ID_37875`) is what the detail panel
walks. That is the field the poke is missing. It does **not** change the outline color, but it
**is** why the half-scan's info panel is empty and re-scanning still awards XP (the engine sees
no catalogued members ⇒ "not yet fully catalogued" ⇒ XP still owed).

---

## 5. REF-FREE FEASIBILITY — verdict + recipe

**Verdict: the `+0x08` catalogue array is ref-free *constructible in principle* but
*materialization-bound in practice*, AND it is unnecessary for green.**

- **The contents are static** (sibling/co-biome species form-IDs are authored in the biome
  data; the same set the ESM/PPBD parse already yields per planet). You *could* hand-build a
  `BSTArray<u32>` of them and stage it.
- **The engine path that fills it requires the live biome** (`ID_937609+0x160` via `ID_56887`/
  `ID_83025`/`ID_83053`), i.e. the player on-planet with the biome materialized — the same
  wall every other on-planet-only step hits. There is no ref-free engine entry point that
  populates `slot+0x08`; `ID_52158`'s biome pass is the only writer and it reads the current
  biome, not a target planet.
- **A hand-rolled ref-free fill is possible but fragile:** you would have to allocate the
  `uint32[]` with the engine allocator (`ID_35770(n*4,4)` / the `BSTArrayHeapAllocator`),
  populate it with the authored member ids, and write the `{begin,end,cap}` triple into
  `slot+0x08/+0x10/+0x18` (or stage it at `subobj+0x08..0x18` *before* the `ID_124898`
  create so the move at lines 596–601 carries it in). Risk: the array is owned/freed by the
  engine (`ID_35771` on slot teardown / rehash), and its exact element semantics (raw
  authored id vs canonical id) must match what `ID_124900`'s consumer expects, or the panel
  shows wrong/duplicate entries. **Not recommended as a first move.**

### Recommended recipe (green now, panel later)
- **For GREEN:** keep writing `slot+0x21` (`ID_124898`) — it is sufficient and correct. The
  only fix that matters is the **species-key domain** (write under the id `ID_52159` hashes =
  the canonical/rendered-base id), already the open G3a item. `slot+0x08` is irrelevant to the
  outline.
- **For the INFO PANEL / to stop residual XP:** the robust path is the engine's own
  `ID_52158` biome pass (on-planet, which the working `CompleteSurvey` already triggers) —
  that fills `+0x08` for free. A ref-free hand-build of `slot+0x08` is *technically* feasible
  (static member ids + engine-allocated `uint32[]` + write the triple) but is allocator/
  ownership-sensitive and should be prototyped behind the guarded-native latch with a single
  test species before any sweep.

---

## 6. One-line answers to the brief

- **Writer fn for slot+0x08:** `ID_52158` (biome-cluster pass, `ID_56887`→`ID_83025`/
  `ID_83053`→`ID_35755`/`ID_37867` push into `slot+0x08`); `ID_124898`/`ID_124837`/`ID_124839`
  carry the array on slot create/rehash. The mod's `ID_124898` stages an empty array ⇒ NULL.
- **Element type:** `BSTArray<uint32_t>` of co-biome **species form-IDs** (the catalogued
  member list), heap-allocated `uint32[]`. Read by `ID_124900`→`ID_37875` for the info panel.
- **Ref-free verdict:** the green does **not** need `+0x08`; `+0x08` itself is static in content
  but its engine writer is biome-materialization-bound (no ref-free engine entry). A manual
  ref-free fill is possible but allocator/ownership-risky — defer.
- **Green/info gate:** GREEN = `slot+0x21` nonzero via `ID_52159` (no `+0x08`, no bit-0x80
  dependence). INFO PANEL / XP = `slot+0x08` non-empty. They are independent. The mod's poke
  greens (modulo the species-key domain) but leaves the panel/XP unfinished because it omits
  `slot+0x08`.
