# Starfield `.sfs` Save File Format — State of the Art

Research snapshot: 2026-04-15.

## Summary

| Aspect | Status |
|--------|--------|
| Format spec (public) | **Partial** — container + metadata only |
| Existing Starfield save parsers | 1 usable ([StarfieldSaveTool](https://github.com/Nexus-Mods/StarfieldSaveTool), no LICENSE) |
| Xbox↔Steam converters | Several: LukeFZ, HarukaMa, Z1ni |
| Skyrim / FO4 parser reuse | Not drop-in — structures evolved, engine version jumped 11→27 |
| Knowledge-DB byte layout | **Unexplored publicly** — no community RE notes published |
| Corruption risk | Real; Bethesda shipped save-fix patches (1.9.67 Feb 2024) |
| Cross-version stability | Format revs across major updates; per-version schema required |

## What StarfieldSaveTool gives us

Covers: zlib chunk decompression, header (engine version 27, format version 140/143+), player metadata, plugin tables (native/light/medium splits), and pointers to data blocks. Explicitly **skips** ChangeForm records, globals, quest data, scripts, form-ID table body — which is exactly where per-planet survey state likely lives.

Repo: https://github.com/Nexus-Mods/StarfieldSaveTool (C#, .NET). **No LICENSE declared** — any fork/reuse requires contacting the author (Silarn / Nexus Mods) first.

## Feasibility estimate (from-scratch)

| Phase | Effort |
|-------|--------|
| Container + header (already done) | reuse |
| ChangeForm table parser + form-ID resolution | 2-4 weeks |
| Locate survey state in decompressed data | **3-6 weeks** (dominant unknown) |
| Safe write + test matrix | 1-2 weeks |
| Patch-churn maintenance | 0.5 week per format bump |
| **Total** | **6-12 person-weeks** |

## Recommended first experiment (cheap, high-info)

Before committing to the multi-week build, do a save-diff:

1. Start a save with an **unsurveyed** planet clearly in view (e.g. Skink).
2. Scan the planet fully to 100% (by any means — console, landing, etc).
3. Save again.
4. Decompress both saves using StarfieldSaveTool.
5. Binary-diff the resulting `.dat` files.
6. Cross-check: repeat with a *different* planet's save pair. Intersection of diffs = global state; symmetric difference = per-planet state.

Total cost: a few hours. Outcome: either we see a clean, localized byte pattern (**viable**), or the diff is scattered across many chunks (**not viable**).

## Tool inventory

| Tool | Purpose | Maturity | Link |
|------|---------|----------|------|
| StarfieldSaveTool (Silarn) | Decompress + metadata JSON, experimental round-trip | Active | https://github.com/Nexus-Mods/StarfieldSaveTool |
| StarfieldSaveConverter (LukeFZ) | Xbox↔Steam | Works | https://github.com/LukeFZ/StarfieldSaveConverter |
| starfield-xgp-import (HarukaMa) | Xbox container | Works | https://github.com/HarukaMa/starfield-xgp-import |
| XGP-save-extractor (Z1ni) | Xbox extraction | Maintained | https://github.com/Z1ni/XGP-save-extractor |
| FallrimTools / ReSaver | Skyrim/FO4 only, **no Starfield support** | Mature | https://github.com/mdfairch/FallrimTools |

## Reference material (best available roadmap)

- UESP Skyrim save format: https://en.uesp.net/wiki/Skyrim_Mod:Save_File_Format
  - [QUST ChangeForm](https://en.uesp.net/wiki/Skyrim_Mod:Save_File_Format/QUST_Changeform)
  - [FLST ChangeForm](https://en.uesp.net/wiki/Skyrim_Mod:Save_File_Format/FLST_Changeform)
  - [Globals](https://en.uesp.net/wiki/Skyrim_Mod:Save_File_Format/Global_Variables)

Starfield extends this family — structures differ but the shape is recognizable.

## Conclusion

Save-file editing is **not easier** than the in-game approach. It's a different RE problem of similar magnitude, with less community groundwork available than for runtime RE. However, the save-diff experiment is cheap enough to run before committing. If the diff is localized, the path becomes viable; if not, it's a dead end confirmed with 1 day's work instead of 6 weeks.
