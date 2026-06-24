# Scan-count runtime store — serialize/deserialize PROOF + save-diff (2026-06-23)

Closes the one `[inferred]` gap in `trait-scan-target-durable-store-2026-06-23.md` §5 and
`re/save/save-file-write-feasibility-2026-06-23.md` §5: those docs asserted the scanned trait
persists across reload via "the engine's ordinary **changed-REFR save** of the specific loaded
persistent REFR, keyed by the runtime REFR." That was an inference, never decompiled, and it is now
**REFUTED** by (a) the decompiled SetScanned path (no REFR dirty-flag), (b) the decompiled
serialize/deserialize of the only saved store, and (c) a byte-exact diff of three real saves that
differ by exactly one hand-scan each.

Confidence tags: `[decompile-verified]`, `[save-verified]` (byte-exact diff of real `.sfs`),
`[inferred]`.

Dumps generated this session (in `re/ghidra/output/`): `scs-serialize.txt`, `scs-materialize.txt`,
`scs-initialscan.txt`, `scs-attach.txt`, `scs-loadrestore.txt`, `scs-initstatus.txt`, `scs-xrefs.txt`,
`scs-xref2.txt`. Save diff scripts/run: `re/save/diff_real_fidarray.py`, `re/save/diff_changeforms.py`,
`re/save/diff_all_regions.py`, `re/save/decode_records.py` (saves Save12/13/14 = 0of2/1of2/2of2).

---

## ★ ONE-LINE ANSWER

The `939118 +0x28` scanned byte is **NOT serialized** (empty stub `ID_38417`) and is **reset to 0 on
every materialization** (`ID_83004`/`ID_83028`/`ID_83043`, all `*(u8*)(comp+0x28)=0`), with **no
durable restore in the scan-target branch** — `[decompile-verified]`. The only durable save record a
scan writes is the **planet-keyed `938333` BSGalaxy::PlayerKnowledge singleton ChangeForm** (real
serializer `ID_52214`→`ID_52193`; real deserializer `ID_51485`→`ID_51523`/`ID_124656`), and the
three-save diff confirms a scan **adds exactly one ChangeForm (count 15362→15363) then edits in place
(15363→15363) with the formID array byte-identical and the scan-target REFR FormIDs absent from every
save** — `[save-verified]`. There is **no per-REFR ChangeForm** for a scan-target (the prior §5 claim
is wrong: a REFR ChangeForm would put the REFR FormID in the save's formID array, and it is not
there). So the only durable, ref-free-writable store is `938333`, which the trait **outline/count do
not read** — meaning off-planet synthesis cannot move the trait `N/M`/outline; that path stays
on-planet / materialization-bound.

---

## Q1 — HOW the scanned byte persists & is restored (serialize + deserialize, decompiled)

### A. The `939118` ScannableComponent `+0x28` byte is NOT serialized. `[decompile-verified]`

- **Empty save serializer.** The ScannableComponent factory's serialize slot is the trivial stub
  `ID_38417` (`scs-materialize.txt:501`): zeroes the descriptor, returns length tag `0x7c`. No bytes
  of the component (and so not `+0x28`) are written to a save.
- **Reset to 0 on every materialization.** All three create/attach materializers stamp the scanned
  byte to 0 and never read it back from any store:
  - `ID_83004` (`scs-materialize.txt:194`): `local_60 = local_60 & 0xffffff00; … *(uint*)(puVar5+5) = local_60;` → `+0x28 = 0`; it stamps only `+0x20`(authored id) / `+0x24`(canonical id, from `ID_83006`).
  - `ID_83028` (`scs-attach.txt`, identical body): `*(uint*)(puVar5+5) = uStack_60;` with `uStack_60 &= 0xffffff00` → `+0x28 = 0`.
  - `ID_83043` (`scs-materialize.txt:75`): `*(u8*)(puVar8+5) = 0;` → `+0x28 = 0` (sets `+0x20`/`+0x24` from the command's `iVar4`/`iVar10`, never `+0x28`).
  - `ID_83029` runtime re-create (`scs-materialize.txt:1`) copies `+0x10`/`+0x18` from a **LIVE source** `*(longlong*)*puVar5`, not a durable record.
- So on cell load the scanned byte starts at **0**; nothing in the scan-target path re-derives it
  from a saved store (see Q3 for the load-time attach `ID_64340`→`ID_83005(...,InitialScanStatus=0)`).

### B. The ONLY durable save record a scan writes is `938333` PlayerKnowledge. `[decompile-verified]`

- **Serializer (Save):** `ID_52214` (`scs-serialize.txt:1`) gates on the PlayerKnowledge type tag
  `*(p+0x2e) == 0xBA` then calls `ID_52193` (`:17`), which looks the component up by
  `(938333<<48) | (*(p+0x54) /*planetId*/ <<16)` in `db+0x268` and serializes its per-species/target
  sub-table (`ID_51421`/`ID_45726`/`ID_51422`/`ID_51420`). Registered as **DATA** in the save-factory
  vtable (xref `ID_52214` from `144b44f98` `(DATA)`, `scs-xrefs.txt:38`) — i.e. it is a savegame
  GlobalData/ModuleState serializer slot.
- **Deserializer (Load):** `ID_51485` (`scs-serialize.txt:176`) → `ID_51523` (`:232`) /
  `ID_124656` (`:396`) rebuild the component into `db+0x268` as
  `StoredComponent<1, BSGalaxy::PlayerKnowledge>` (the `TObjectWrapper<…PlayerKnowledge>::vftable`
  store). DB-insert command = `ID_51444` / `ID_52204` (`CreateAndDeleteCommand<…PlayerKnowledge>`,
  `:296`/`:346`). Registered as DATA at `1485f7fe4` (`scs-xrefs.txt:43`).
- **The write that fires on scan:** `SetScanned`=`ID_118472` → `ID_83008` cVar==0 branch
  (`scs-materialize.txt:466`): `ID_83038` stamps the transient `939118 +0x28` AND extracts the
  canonical id; if non-zero it calls `ID_52157`→`ID_52158` (`scs-serialize.txt:635`/`:742`) which keys
  `(938333<<48)|(planetId<<16)` (planetId from `ID_52188`) and writes slot `+0x21` via `ID_124898`
  (`:468`). This is the record that ends up in the save.

**ChangeForm changeFlag / extra-data type:** the persisted state is **not** a REFR ExtraData and
**not** a REFR changeFlag. It is a **BSGalaxy GlobalData/ModuleState ChangeForm** (the saved
PlayerKnowledge component, `discriminator 938333`), keyed by **planetId**, serialized by `ID_52214`
and restored by `ID_51485`. (Type-0x81 `ExtraLocation` on the ref is only a *write-time planet
redirect* for `ID_52188`, not the persisted scanned state — see `extradata-0x81-spoof-verdict`.)

---

## Q2 — Why a static REFR's state saves with its FormID absent from the formID array

**Because there is NO REFR ChangeForm.** `[decompile-verified]` + `[save-verified]`

- **SetScanned never dirties the REFR.** `ID_118472`/`ID_83008`/`ID_83038`/`ID_52157`/`ID_52158`
  contain **no call to the REFR's AddChange** (`(**(code**)(*refr+0xb8))(refr, flag)`) and no REFR
  ExtraData write. (Contrast `ID_64340`, the 3D-attach path, which *does* call `+0xb8` with model/
  visibility flags `0x80000000`/`0x80`/`0x400` — but that is graphics attach, fired on materialize,
  not the scanned state.) A ref that is never AddChange'd gets **no ChangeForm**, hence its FormID is
  never added to the save's formID array.
- **Save-diff proof (three real saves, one hand-scan apart):** `[save-verified]`
  - `diff_real_fidarray.py`: formID array is **byte-identical** across 0of2/1of2/2of2 — count
    **311117** in all three, **0 added / 0 removed / divergence at index 311117 (= none)**. The
    trait scan-target REFRs `0x00159EB2 0x00159F10 0x0016776F 0x00167770 0x002EA231 0x002EA0D1` and
    base ACTI `0x0021B250` are **absent from the array entirely** (`decode_records.py`: "low24
    matches at indices (NONE)" for every one).
  - `diff_changeforms.py` / `loc_table`: `changeFormCount` = **15362 → 15363 → 15363**. The first
    scan **adds exactly one ChangeForm**; the second **edits in place** (count unchanged). An added
    ChangeForm with **no formID-array entry** is, by construction, a **GlobalData/singleton-keyed**
    ChangeForm — exactly the BSGalaxy::PlayerKnowledge ModuleState record from Q1, not a REFR record.
  - `diff_all_regions.py` shows the structural delta: small in-place flag flips plus +20/+40-byte
    record insertions and one record that **grows in place** (e.g. `0of2→1of2` region#15 +169 B of
    float/coordinate data, region#12 +21 B), i.e. a singleton container record gaining sub-entries —
    the signature of a per-planet knowledge table growing as targets are scanned, NOT a new
    per-REFR form.

**Conclusion Q2:** the scanned scan-target is **not** a dynamically-created FF-space ref, and its
state is **not** in a REFR ChangeForm. It is carried entirely by the planet-keyed `938333`
PlayerKnowledge singleton ChangeForm. That is why the static REFR FormID never appears in the save's
formID array — there is no per-REFR record at all.

---

## Q3 — The in-memory store/key to write off-planet, and the proven limit

### What is durable and ref-free-writable
The `938333` PlayerKnowledge slot, key **`(938333<<48) | (planetId<<16)`** for the planet-level
component, sub-slot keyed by **canonical id** (for a scan-target = its **base ACTI FormID**, ESM-
derivable; `ID_83006`→`ID_64338` false→`ID_63393` default), byte at sub-object **`+0x21`**, written
ref-free via `ID_124898(subobj, canonicalId, 0xFF)` (`scs-serialize.txt:468`). This IS saved
(`ID_52214`) and restored (`ID_51485`).

### Why writing it does NOT move the trait outline / `N/M` count — `[decompile-verified]`
The trait scan-target's **outline color and `N/M SCANNED` count read ONLY the transient
`939118 +0x28`**, never `938333`:

- Outline reader `ID_83007` (full body in `scanned-state.txt`) has **three** branches on the base
  `0x2a` flag (`ID_36022(base+200,0x2a)`):
  - **cVar==0 (the scan-target / flora branch):** returns `(component[939118 +0x28] != 0) + 1`,
    looked up purely by `(939118<<48)|(refFormID<<16)`. **No `938333` fallback.**
  - cVar!=0, normal-range formID: durable `938333` via `ID_52162` (`scs-initstatus.txt:1`, reads
    `938333` sub-table) — species path only.
  - cVar!=0, dynamic formID: durable `938333 +0x21` via `ID_52159` (`scs-initialscan.txt:391`).
  The scan-target takes the **cVar==0** branch, so its outline is the transient byte alone.
- Count walker `ID_90522` range-scans `db+0x268` for `939118` entries and reads each `+0x28`
  (documented `trait-scan-target-durable-store-2026-06-23.md` §★★). It does **not** consult `938333`.
- Load-time attach re-creates the component with `+0x28 = 0` and InitialScanStatus 0
  (`ID_64340`→`ID_83005(ref, ID_83006(ref), 0)`, `scs-xrefs.txt:19`; `ID_97254`→`ID_83005(…,0)`,
  `scs-loadrestore.txt`). No decompiled path re-stamps `939118 +0x28` from `938333` for a scan-target.

So the durable `938333` write a scan performs is consumed by the **survey-% aggregator** (species
model `ID_97851`/`ID_52162`), not by the scan-target outline/count. Writing `938333` ref-free
reproduces the species "100%/credit but still BLUE, `0/M`" failure for scan-targets.

### Definitive verdict (decompiled serialize+deserialize, not assumption)
- The **only** durable persistence reachable by a scan is the planet-keyed `938333` PlayerKnowledge
  ChangeForm (serialize `ID_52214`/`ID_52193`; deserialize `ID_51485`/`ID_51523`/`ID_124656`) — and
  it does **not** drive the trait outline/count.
- The trait outline/count byte (`939118 +0x28`) has **no serializer** (`ID_38417` stub) and **no
  load restore** in its branch; it is created-from-zero on materialization and deleted on detach
  (`ID_126691` delete-on-detach; `trait-scan-target-durable-store` §★★). It **cannot be synthesized
  off-planet** because the component does not exist until the overlay cell materializes on the
  surface, and even then it is rebuilt at 0 with no durable source.
- The earlier `[inferred]` "changed-REFR save re-stamps `+0x28` on re-materialization" is **false**:
  the save-diff shows **no per-REFR record** (formID array byte-identical; REFR FormID absent), and
  the decompile shows **no AddChange** on the ref and **no `938333`→`939118` restore** in the
  scan-target branch. What survives reload is the planet-keyed `938333` knowledge record; it credits
  survey-% but does not, by any decompiled reader, re-green the scan-target outline or move its
  `N/M` — consistent with the trait green being **on-planet / loaded-ref bound**.

**Net:** to move a scan-target's `N/M`/outline you must set `939118 +0x28` on the **loaded ref**
on the planet (`ID_118472`/`ID_83008` on the materialized REFR). There is **no** off-planet runtime
store and **no** save record you can write to do it; the one ref-free durable store (`938333`) has
no reader for this surface element.

---

## Evidence index
- Decompile: `scs-serialize.txt` (52214/52193 Save, 51485/51523/124656 Load, 124898 slot write,
  52157/52158 durable write), `scs-materialize.txt` (38417 stub; 83004/83028/83043 +0x28-reset;
  83029 live-copy; 83008/83038 SetScanned), `scanned-state.txt` (83007 three-branch outline reader),
  `scs-initstatus.txt` (52162 938333 reader), `scs-attach.txt`/`scs-loadrestore.txt` (64340/97254/
  47227 attach → 83005 InitialScanStatus=0), `scs-xrefs.txt`/`scs-xref2.txt` (52214/51485 = save
  DATA slots; 83007→52159; count walkers).
- Save diff (byte-exact, `C:\Users\…\Starfield\Saves\Save12/13/14`): `re/save/diff_real_fidarray.py`
  (formID array byte-identical, count 311117, scan-target FormIDs absent), `re/save/diff_changeforms.py`
  (changeFormCount 15362→15363→15363), `re/save/diff_all_regions.py` (insert-one / edit-in-place,
  singleton record growth), `re/save/decode_records.py` (trait FormIDs NONE in array).
- Prior: `trait-scan-target-durable-store-2026-06-23.md`, `save-file-write-feasibility-2026-06-23.md`
  (this doc supersedes their `[inferred]` §5 changed-REFR claim with decompile+save-diff proof).
