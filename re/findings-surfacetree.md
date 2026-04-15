# BGSPlanet::PlanetData / BGSSurface::Tree — Empirical Findings (1.16.236.0)

Session: 2026-04-15. Data source: live hex dumps via `CompletePlanetSurveyNative.DumpPlanetLayout` on planet `0x0005E05B` (Skink-class, partial survey, biome-heavy).

## BGSPlanet::PlanetData (kPNDT, formType 0xBA)

Confirmed offsets — corrects CommonLibSF's `extern/CommonLibSF/include/RE/B/BGSPlanetData.h`:

```
+0x00..+0x2F  : TESForm (vtable, flags, FormID at +0x28, FormType at +0x2E)
+0x30         : 8 bytes ZERO  <-- CommonLibSF says surfaceTree is here; it's not
+0x38         : BGSSurface::Tree* surfaceTree  <-- REAL location
+0x40..+0x4F  : floats (gravity/temp/pressure/etc — matches CommonLibSF's partial layout)
+0x50..+0xAF  : BSTArray<BGSKeyword*> (11 entries observed for Skink)
                Each entry is kKYWD (formType 0x04): traits, star class, planet
                class, water type, atmosphere — NOT biomes.
```

## BGSSurface::Tree (kSFTR, formType 0xB6) — header

A per-planet spatial tree. Observed fields within the first 0x70 bytes:

```
+0x00..+0x2F  : TESForm
+0x28         : FormID of the SFTR record (0x00169A03 for Skink's tree)
+0x2E         : formType 0xB6
+0x40         : uint32 = 0x40  (64 — maybe total tile count)
+0x44         : uint32 = 5     (likely BIOME COUNT)
+0x48         : uint32 = 3
+0x4C         : uint32 = 0x011000
+0x50         : uint32 = 0x100
+0x54         : uint32 = parent planet FormID  <-- back-reference
+0x58         : uint32 = 0x17C3A
+0x5C         : uint32 = 1
+0x60         : uint32 = 0x22000  (spatial bound? texture size?)
+0x64         : uint32 = 0x22000
+0x68         : pointer to 16 x uint64 array  <-- tile/child array
+0x70..       : next tree entry in heap (not part of this tree)
```

## tree+0x68 target (16-entry tile array)

Pattern observed — 16 entries, stride 0x10:

```
[ 0] 0x0000_0000_0000_1000
[ 1] 0x0000_0000_0000_1010
[ 2] 0x0000_0000_0000_1020
[ 3] 0x0000_0002_0000_1030  <-- upper bits nonzero
[ 4] 0x0000_0000_0000_1040
...
[15] 0x0000_0000_0000_10F0
```

These are not BGSBiome pointers. Likely spatial tile IDs or LUT keys.
The upper-bits flag on index 3 suggests per-tile metadata (maybe "this
tile has a specific biome assignment").

## tree+0x70, tree+0x80 targets

Pointers into EXE space (`0x7FF7...`). Likely engine-owned vtables or
shared constant tables — not planet-specific data.

## Consequences

The tree cannot be naively "walked for biome pointers" — biomes are
encoded via an opaque tile/index system. To enumerate biomes for a
planet without landing, we need one of:

1. Decompiled `ID_83010` / `ID_83012` (biome-fauna / biome-flora enum
   dispatchers) to understand how `(planet_id, biome_id)` pairs work.
2. Decompiled `ID_47749` (suspected biome-load populator called from
   `ID_97857`) — may be invokable standalone to force slot materialization.
3. Find the engine's internal "for each biome on this planet" loop and
   call it directly.

## Empirical test baseline

Remote-scan completion after hooking OnShipScan (alias-based), 2026-04-15:

| Planet    | Pre-scan | Post-mod | Notes                        |
|-----------|----------|----------|------------------------------|
| 0x5E0D1   | 86%      | **100%** | Simple planet, saturates     |
| 0x5E0D6   | 86%      | **100%** | Simple planet, saturates     |
| 0x5E05B   | 35%      | 76%      | Biome-heavy, 1+ slots missing |
| 0x5E063   | 48%      | 68%      | Biome-heavy                  |
| 0x5E064   | 37%      | 75%      | Biome-heavy                  |
| 0x5E065   | 44%      | 70%      | Biome-heavy                  |

The gap corresponds to biome-specific flora/fauna/resource slots that
are lazy-populated on landing. Materializing them from orbit is the
remaining engineering problem.

## Biome-enum dispatch path — confirmed via full decompilation

`re/ghidra/output/biome-body.txt` contains full Ghidra pseudocode for
`ID_83010`, `ID_83012`, `ID_47749`.

### ID_83010 body (at `141307af0`)

```c
void ID_83010(uint32 planet_id, uint32 biome_id, void* out_arr, uint64 weight)
{
    db = ID_126578()->knowledge_db;  // at db+0x8B0
    cVar5 = ID_51413(&db, planet_id, biome_id, &biome_state);
    if (cVar5 != 0) {
        ID_83011(struct{planet_id, &biome_state, out_arr, &weight}, &db);
    }
}
```

### ID_51413 — the validation gate (why we return count=0)

Iterates a per-planet "biome entries" container and matches biome_id
against entries' `+0x60` field:

```c
entry = *(lVar + 0x18) + *(ushort*)(lVar + 0x12 + *(lVar + 0x20)*4);
if (*(int*)(entry + 0x60) == param_3 /*biome_id*/) { return 1; }
```

The container appears to be a hashmap at offset `db + 0x300`, keyed by
`(biome_id << 32) | discriminator | 0x10000`. Entries are populated when
biomes are discovered / loaded at runtime. **For un-landed planets, no
entries exist — so no biome_id ever matches**, `ID_51413` returns 0,
`ID_83010` bails, output is never written.

### ID_83011 — the actual enumerator (post-validation)

- Output buffer: data array pointer at `param_1[2] + 8`, stride 8 bytes
- Weight threshold: `*(byte*)param_1[3]` — includes species whose scan
  flag byte (at `entry + 0x20`, stride 0x30) is `<= threshold`

### The underlying wall

Our mod cannot fabricate `biome_id` values that `ID_51413` will accept
without the per-planet biome container being populated first. That
container's writer is the unsolved piece. Candidates:

- `ID_47749` — currently looks like a reader, not a writer.
- `ID_97857` — first agent called it "slot materializer"; unconfirmed.
- An engine function invoked during biome-cell load on landing — the
  most likely writer, but needs identification.

## Alternative angle: offline save-file editing

Rather than manipulating the live knowledge DB, write scan-flag bytes
directly into the serialized knowledge DB in the `.sfs` save file.
Pros: bypasses runtime materialization entirely. Cons: needs Starfield
save format RE and introduces save-compat risk across game updates.
Not pursued this session — captured as a future option.
