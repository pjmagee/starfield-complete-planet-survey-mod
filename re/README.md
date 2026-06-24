# `re/` — reverse-engineering workspace

Everything used to reverse-engineer Starfield's planet-survey internals for this mod.

**Start with [`FINDINGS.md`](FINDINGS.md)** — the curated index of the *confirmed* results
(green/species, traits, resources, the in-world trait object, save format) plus the confirmed
engine offsets / REL::ID table and the list of disproven dead-ends. The raw dumps in here are the
working notes behind that index; the index is the source of truth.

> Engine target: Starfield **1.16.236 ≡ 1.16.244** (struct offsets identical), SFSE 0.2.21.
> Offset convention: runtime offset = decompile address − `0x140000000`.

## Layout

| Dir | What | Regenerate with |
|---|---|---|
| `ghidra/output/` | Decompile/disassembly dumps (`*.txt`) + synthesis write-ups (`*.md`). The `*-2026-06-*` dated files are iterative; the `*.md` files are the consolidated conclusions. | `analyzeHeadless.bat` + the scripts in `ghidra/scripts/` (see below) |
| `ghidra/scripts/` | Ghidra headless Java scripts (`Dump*`, `Find*`, `Xrefs*`, `ImportAddressLibrary.java`) + the Python `offset_skew*.py` version-diff. | — |
| `tools/` | Offline ESM analysis in Python (`esm_*.py`). **`esm_derive_markers.py` is the reference green-marker deriver** (validated 17/17). Outputs `trait_scan_target_map.json`. | `python re/tools/esm_derive_markers.py --validate` |
| `esm/` | Earlier/ad-hoc ESM extraction + the BA2 helpers. `extract_planet_species.py` (PNDT/PPBD), `handscanner_kywds.json` (87-marker registry), `pex/` (decompiled base-game Papyrus). | `python re/esm/extract_planet_species.py` |
| `save/` | `.sfs` save-file parsing + byte-diff scripts. `sfs_container.py`/`sfs_body.py` = the container/body parser; `decode_pk_record.py` = the durable `938333` record; `analyze_scan_count.py` / `compare_save*.py` / `verify_render_gate.py` = the diff harnesses. | `python re/save/analyze_scan_count.py` (needs the user's Save12/13/14 etc.) |
| `frida/` | Live `Starfield.exe` probes (`probe_*.py`). **Offsets are currently unreliable** — re-derive via REL::ID before use; never hook per-frame functions (crashes). See FINDINGS "Frida caveat". | `frida -p <pid> -l re/frida/probe_*.py` |

## Ghidra project
Auto-analyzed `Starfield.exe` with the Address Library imported as `ID_<n>` labels (~910k).
Headless usage:
```
analyzeHeadless.bat <ghidra-project> Starfield -process Starfield.exe -noanalysis \
  -scriptPath re/ghidra/scripts -postScript DecompileIds.java <out.txt> <ID> [<ID> ...]
```
Pass IDs as separate positional args (not comma-joined). `offset-skew-236-vs-244.md` documents the
fast-path (versionlib + capstone) route used to confirm offsets across game versions without a re-import.

## Conventions
- Dated filenames (`*-2026-06-DD`) are point-in-time iterations; later dates supersede earlier ones
  on the same topic. The `.md` synthesis files fold the `.txt` probes into a conclusion — prefer them.
- The deepest narrative history lives in the session's local Claude memory (not in git); `FINDINGS.md`
  is the in-repo distillation of the *confirmed* parts of that history.
- In-game behaviour can only be verified by the user (the running game can't be automated), so
  `in-game`-tagged findings in `FINDINGS.md` came from the user, not from any tool here.
