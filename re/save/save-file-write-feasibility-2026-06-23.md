# Save-file write completion of trait scan-targets — feasibility (2026-06-23)

**Question:** can we write planet-survey *trait* completion (green outline + N/M count + identity)
directly into the `.sfs` save so the engine loads them already-complete, ref-free, all-planets —
bypassing live-memory writes?

**Verdict: BLOCKED. Save-file-write completion of trait scan-targets is NOT achievable**, for two
independent, mutually-reinforcing reasons:

1. **No durable record to write** (the *semantic* blocker, already proven by decompile, now confirmed
   empirically against real saves): the trait outline / "N/M" count / identity read a **transient
   per-ref `939118` ScannableComponent `+0x28` byte** that has an *empty save serializer* and is
   *reset to 0 on materialization*. It is never written to the save, so there is no byte in the save
   to set. The static trait scan-target REFR FormIDs **do not appear anywhere in any save** (verified
   in 4 saves incl. on-surface saves *at* the trait location, in 4 byte-encodings) — there is nothing
   to find, let alone modify.

2. **The format is not safely writable** (the *mechanical* blocker): Starfield's decompressed body is
   FO4/SSE-*shaped* (header → plugins → file-location table → global-data tables → ChangeForms →
   formID array), but the **record encoding inside the ChangeForms / GlobalData / formID-array
   sections is undocumented and diverges from FO4** (confirmed: the community reference tool,
   Nexus-Mods/StarfieldSaveTool, explicitly stops at the header/plugin metadata — "rest of file has
   not been worked out yet"; and our own attempts to walk the global-data records and the formID array
   with the FO4 `(u32 type,u32 len)` / `(u32 count,u32[])` conventions produced implausible
   type/length/count values, i.e. the encoding is genuinely different). Even if a target byte existed,
   we could not locate or resize records around it without risking corruption.

Either blocker alone is fatal. Together they make the save-write path a dead end. This matches and
reinforces the live-memory RE conclusion in `re/ghidra/output/trait-true-completion-2026-06-23.md` §4
and `trait-scan-target-durable-store-2026-06-23.md`: trait green is **on-planet / loaded-ref bound**.

---

## 1. The `.sfs` (BCPS) container format — fully decoded, parser works

Magic `BCPS` ("Bethesda Compressed Plugin Save"). Header (all little-endian):

| file off | type | field | observed |
|---|---|---|---|
| 0x00 | char[4] | magic | `BCPS` |
| 0x04 | u32 | version | 1 |
| 0x08 | u64 | chunkHeaderSize | 0x48 |
| 0x10 | u64 | (zero) | 0 |
| 0x18 | u64 | **dataStart** (offset of compressed payload) | 0xA0 |
| 0x20 | u64 | **uncompressedTotal** (sum of decompressed chunks) | 0x54C844 |
| 0x28 | u64 | flags? | 0x40000000 |
| 0x30 | u64 | ? | 0x40000 |
| 0x38 | u64 | ? | 0x10 |
| 0x40 | u32 | chunk id/seed | 0x9F371F03 |
| 0x44 | char[4] | **compressor** | `ZIP ` (zlib) |
| 0x48 | u32 | per-chunk uncompressed size | 0x33E99 (≈212 KB) |
| 0x4C.. | u32[] | per-chunk **compressed** sizes | (21–23 entries) |
| 0xA0 | … | compressed payload (concatenated zlib streams) | `78 5E …` |

The payload is a series of independently-zlib-compressed chunks (each ≈212 KB uncompressed),
concatenated, padded to 16-byte alignment. Decompress each stream in turn (`zlib.decompressobj`,
`unused_data` marks the next stream; skip padding to the next `78 {01,5E,9C,DA}` zlib header).
Decompressing all chunks yields exactly `uncompressedTotal` bytes — **verified to the byte** on every
save tested. Compressor observed is always `ZIP ` (zlib/Deflate, fast); LZ4 path stubbed but unused.

Parser: `re/save/sfs_container.py` (`BCPSContainer.decompress()` → body bytes).

## 2. The decompressed body (`SFS_SAVEGAME`) — documented part decoded, rest is the wall

Body begins with ASCII magic `SFS_SAVEGAME`, then:

| field | type | notes |
|---|---|---|
| headerSize | u32 | 0x91 = 145 |
| header | bytes[headerSize] | char name ("Fresh Character"), save#, **location string** ("Jemison - Sentient Microbial Colony"), playtime, "HumanRace", level, gender, XP, FILETIME, etc. |
| saveVersionByte | u8 | 153 (0x99) |
| currentGameVersion | wstr (u16 len) | "1.16.244.0" |
| createdGameVersion | wstr | "1.16.244.0" |
| pluginInfoSize | u16 | 0x101E = 4126 |
| pluginInfo | block | masters: `Starfield.esm`, `ShatteredSpace.esm`, …, **`CompletePlanetSurvey.esm`** present |

Parser: `re/save/sfs_body.py`. It reads the documented header/version fields reliably. The plugin
walk reads the leading FULL master names but the per-plugin **extraInfo** encoding for
saveVersion≥140 (creation name/id + variable-length flags + achievementCompatible byte) is only
partially documented, so the walk is capped and not relied upon.

### The FO4-style file-location table (located structurally, contents undocumented)

Right after the plugin block sits a **file-location table** identical in *shape* to FO4/SSE. We
locate it structurally (4 ascending section offsets < body length, then the 3 small global-data-table
counts 17/21/9). For the newest save:

```
table_base        = 0x10E4
offsetA           = 0x1140     (data region / formID-array-count area, table-relative)
offsetB           = 0x78C44    (global data table region)
offsetC           = 0x7C848    (global data table region)
changeFormsOffset = 0x29B478   (ChangeForms region)
gdt1Count=17  gdt2Count=21  gdt3Count=9
changeFormCount   = 15363
formIDArrayOffset = 0x4C052C
```

These offsets are stable in shape across saves (Save4/Save8/Save10/Exitsave all match). **But the
byte encoding INSIDE each section is not FO4-compatible:**

- Walking GlobalDataTable1/2/3 as FO4 `(u32 type, u32 length, bytes[length])` records yields
  type=2,584,380,458 / length=1,073,774,592 etc. — garbage. Starfield's global-data record framing
  differs.
- Reading the formID array as FO4 `(u32 count, u32[count])` yields count=0x90C3158 (151 M) — garbage.
  The formID re-indexing / encoding differs.

So past the plugin list we can *bound* the sections but cannot *parse* them — exactly the documented
state of the art (StarfieldSaveTool: "rest of file has not been worked out yet"). Where the FO4 model
ends is precisely where the trait state would have to live.

## 3. Empirical search for trait completion state — it is not in the save

Tool: `re/save/scan_body.py`. Searched 4 decompressed bodies for the RE-identified targets:

- trait-20 Jemison scan-target REFRs `0x00159EB2 0x00159F10 0x0016776F 0x00167770 0x002EA231
  0x002EA0D1`
- base ACTI `0x0021B250`, keyword `0x001CBEA3`, LocRefType `0x0027A567`
- ASCII tags `PlayerKnowledge`, `Scannable`, `BSGalaxy`, `ChangeForms`, `GlobalData`, …

in 4 encodings (LE32, BE32, low-24 LE, 24-bit BE), across:

| save | location | trait REFRs found? | ASCII knowledge tags? |
|---|---|---|---|
| Save4  | Jemison **Landing Area** (pre-trait) | **none** (all encodings) | none |
| Save8  | Jemison **Sentient Microbial Colony** | **none** | none |
| Save10 | Jemison **Sentient Microbial Colony** | **none** | none |
| Exitsave0 | Jemison **Sentient Microbial Colony** | **none** (one 3-byte LE24 coincidence for `0x00167770` — absent in the other 3 saves ⇒ noise) | none |

**Result:** the static trait scan-target FormIDs appear **nowhere** in **any** save — including
on-surface saves taken *standing on the trait overlay*. There are no ASCII `PlayerKnowledge` /
`Scannable` / section markers (Starfield uses binary type IDs, not ASCII tags). This is the empirical
confirmation of the decompile: the trait scan-target's scanned state (`939118 +0x28`) is transient,
unserialized, and reset on materialize — there is no save record keyed by these FormIDs to write.

(Note: a *real* hand-scan persists across reload not via an id-keyed knowledge record but via the
engine's ordinary changed-REFR save of the *specific loaded persistent REFR* the player scanned. That
changed-REFR entry lives inside the undocumented ChangeForms section, is keyed by the runtime REFR and
re-stamps `+0x28` on re-materialization — it is per-loaded-ref, on-planet, and cannot be synthesized
for a never-visited planet whose overlay REFRs do not yet exist. See §5.)

## 4. The "all-planets / never-visited" angle — doubly impossible

For a **never-visited** planet the scan-target REFRs are not even instantiated (created on first
materialization). To complete them via the save we would have to *fabricate* ChangeForm records keyed
by the static ESM scan-target REFR FormIDs (`0x00159EB2`, …). This fails on every axis:

1. There is no ChangeForm "scanned" field for a scan target to fabricate — the scanned state is the
   transient `939118 +0x28`, which has no serialized representation (empty stub serializer
   `ID_38417`; reset-to-0 materializers `ID_83043`/`ID_83004`; live-source copy `ID_83029`). A
   fabricated record has nothing valid to carry.
2. The ChangeForms section encoding is undocumented (§2) — we cannot author a well-formed ChangeForm,
   cannot update `changeFormCount`, and cannot fix the dependent offsets/formID-array.
3. Even the durable `938333` PlayerKnowledge slot a real scan *does* write (keyed by planetId +
   base-ACTI canonical id, the one ref-free-writable store) is **not read** by the outline/count/
   identity — only by the survey-% aggregator. Writing it (if we even could locate it in the save)
   reproduces the species "100% but still BLUE / 0-of-M / Unknown" failure. It is a write with no
   consumer for this problem.

## 5. Why a scanned trait stays green across reload (reconciliation)

The engine saves the **changed-reference record** of the one loaded persistent REFR the player
physically scanned (the normal mechanism by which any modified placed reference persists). On reload
that REFR re-materializes and the attach path re-stamps its `939118 +0x28` from the restored
changed-ref state. This is **per-loaded-ref, on-planet, materialization-bound** — *not* an id-keyed
knowledge record we can pre-write from orbit, and it only re-greens the exact REFR that was scanned on
the planet that was visited. It lives in the undocumented ChangeForms blob, so we can neither read nor
forge it safely.

## 6. Corruption / risk notes (why we would not attempt the write even if tempted)

- **Resizing is unsafe.** The body carries an internal file-location table of absolute section offsets
  plus a global `changeFormCount` and a formID array. Adding/modifying any record shifts every later
  offset; all of these (offsets, counts, and the formID re-index) would have to be patched
  consistently. With the section encodings undocumented (§2) this cannot be done reliably → high
  corruption risk (the engine rejects or crashes on a malformed save).
- **Recompression must be exact.** After any body edit the BCPS container must be re-chunked (≈212 KB
  uncompressed per chunk), each chunk re-zlib-compressed, the per-chunk compressed-size table at
  0x4C.. rebuilt, `uncompressedTotal` at 0x20 and `dataStart`/payload-size fields fixed, and 16-byte
  padding reapplied. Our container writer is *not* implemented (no need, given the verdict).
- **Operate on copies only.** Any future experiment must work on a copied `.sfs`, never the live
  Saves folder; keep the original; validate by loading in-game before trusting.
- **Honest bottom line:** the container is fully understood and safely *readable*; the body past the
  plugin list is *not* understood well enough to *write* safely, and — critically — **there is no
  trait state in the body to write in the first place.**

## 7. Deliverable files

- `re/save/sfs_container.py` — BCPS container parser + zlib chunk decompressor (verified byte-exact).
- `re/save/sfs_body.py` — SFS_SAVEGAME header/version/plugin reader + structural file-location-table
  locator.
- `re/save/scan_body.py` — multi-encoding FormID / ASCII-tag scanner over a decompressed body.
- `re/save/parse_sfs.py` — end-to-end driver (`.sfs` → container → body → scan), READ-ONLY.
- this writeup.

## 8. If a green-trait save is later provided (the one thing that could add detail)

Nothing here is blocked on data — the FormID-absence result is already decisive. But a diff of a save
where a trait scan-target is fully green vs. an otherwise-identical not-green save would let us
*localize* the engine's changed-REFR entry for that scanned REFR inside the ChangeForms blob (it would
be the delta keyed by that REFR). That would confirm §5 concretely and reveal a sliver of the
ChangeForm encoding — but it would **not** change the verdict: that record is per-loaded-ref and
on-planet, cannot be synthesized for unvisited planets, and the section is still not writable safely.
The recommended completion path remains the **on-planet native** recipe in
`re/ghidra/output/trait-onplanet-completion-2026-06-23.md` §5 (`ID_83008(ref,1,8,0)` +
`ID_83025(...)` on the loaded ref), not a save edit.
