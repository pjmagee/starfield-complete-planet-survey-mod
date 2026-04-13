# Reverse Engineering Starfield.exe

Tools:
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra) — free, scriptable (Python 3 via Ghidrathon, or Jython)
- IDA Pro / Binary Ninja — commercial alternatives
- Address Library (Nexus 3256) — stable ID <-> offset map, the source of truth
- `offsets-1-16-236-0.txt` (and siblings) — per-version offset drops in this repo

## Why Address Library exists

Each Starfield update shifts code. Without a stable abstraction every plugin would break on every patch. The Address Library assigns a **version-independent ID** to each interesting function/global, and ships a per-version binary (`versionlib-1-16-236-0.bin`) mapping ID -> offset. Plugins reference IDs only.

When Bethesda patches:
1. `meh321` (maintainer) regenerates the Address Library for the new runtime
2. Plugin authors rebuild against the new CommonLibSF/versionlib
3. Source usually unchanged unless structs moved

## Ghidra workflow for Starfield

1. **Import** `Starfield.exe` (PE x86-64, Windows).
2. **Analyze** with default options + "Decompiler Parameter ID" on. This takes 30+ minutes on a fresh binary.
3. **Load PDB** if you have one (Bethesda does not ship symbols; community-published `.json` symbol drops exist on some repos).
4. **Import known types** — CommonLibSF header structs can be transliterated into Ghidra's Data Type Manager for better decomp.
5. **Apply Address Library IDs as labels** — iterate the versionlib .bin and rename `FUN_1400XXXXX` to `ID_12345_GetSurveyedResources` or similar. Scripts exist; see `ghidra-project/scripts/`.

## Signature scanning (pattern / AOB)

When a function isn't in Address Library yet, find a byte pattern unique to it:

```
48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ?
```

`?` (or `??`) is a wildcard — typically used for relative call/jump operands that change per build. The pattern must be unique in `.text`. Prefer 16+ bytes.

In code:
```cpp
static REL::Relocation<void(*)()> MyFunc{ "48 89 5C 24 ? 57 ..." };
```

Sig scans are fragile. Promote to an Address Library ID as soon as one is assigned.

## Updating for a new game version

1. Pull the newest `versionlib-*-*.bin` from Nexus 3256.
2. Update CommonLibSF to a commit that declares the new `RUNTIME_VERSION_X_Y_Z_W`.
3. Add the new runtime to your plugin's `compatibleVersions` array.
4. Diff your tracked IDs against the new database — any that were removed/renumbered need reinvestigation.
5. Rebuild. If a struct field moved, CommonLibSF updates will catch most; odd crashes => diff the relevant type in Ghidra.

## Finding a new function from scratch

Typical recipe:
1. Find a **string** that only appears in the target function (Ghidra: Search > For Strings).
2. Xref to the function using the string.
3. Check the function signature and calling convention in the decompiler.
4. Confirm with a runtime hook + log print.
5. If promoting: request an Address Library ID assignment (or generate locally with known naming conventions).

## Headless Ghidra workflow (process we actually use)

Install: Ghidra isn't on winget — download the release zip from
`github.com/NationalSecurityAgency/ghidra/releases/latest` and extract to
`C:/Tools/ghidra_<ver>_PUBLIC/`. Needs JDK 21 on PATH (or set `JAVA_HOME`).

One-time import + analysis (~30 min for Starfield.exe 1.16.236.0):

```bash
analyzeHeadless.bat <project-dir> Starfield \
    -import "<path>/Starfield.exe" \
    -analysisTimeoutPerFile 7200
```

Second pass to label Address Library IDs (must pass `-scriptPath <dir>` so
custom scripts are picked up):

```bash
analyzeHeadless.bat <project-dir> Starfield -process Starfield.exe \
    -noanalysis -scriptPath re/ghidra/scripts \
    -postScript ImportAddressLibrary.java offsets-1-16-236-0.txt
```

Scripts live in `re/ghidra/scripts/`; outputs default to `re/ghidra/output/`
via `getSourceFile()`-relative paths. Add `re/ghidra/scripts/` in Ghidra's
Script Manager → Script Directories for interactive use.

Patterns observed:

- Custom Java scripts can't use `askFile` in headless mode — take paths via
  `getScriptArgs()` instead.
- Pass multiple IDs to a dump script as **separate positional args**, not
  comma-joined — Ghidra tokenizes them on the command line.
- `DefinedDataIterator` is in `ghidra.program.util` and sometimes needs
  explicit resolution; `listing.getDefinedData(true)` is more portable.
- Decompiled `undefined8` params often hide real arguments — inspect the
  call site in assembly (`LEA R9, [addr]` before the call reveals param_4
  that the decompiler lost).

## Finding Papyrus native C++ implementations

For a Papyrus native (declared `foo() native` in `.psc`), the binding site
lives in the class's registrar function. Recipe:

1. `FindSurveyStrings.java`-style: scan `listing.getDefinedData(true)`,
   filter `hasStringValue()` entries matching the native name exactly.
2. Xref the string -> find the registrar (the function containing the
   `BindNativeMethod` call for that name).
3. Inside the registrar, the target impl is passed to a helper like
   `ID_114900(store, vm, "GetSurveyPercent", impl_ptr)`. The `impl_ptr` is
   the arg the decompiler routinely elides.
4. Read raw assembly around the call site. Windows x64 calling convention
   puts args 1-4 in RCX, RDX, R8, R9. The `LEA R9, [<addr>]` instruction
   before the `CALL` loads `impl_ptr`.
5. Map the address back to an Address Library ID by grepping the
   `offsets-*.txt` file (`grep -E "\s141F1B750$" offsets.txt` -> ID).

## Event-driven vs state-driven writes

Many Bethesda engine systems are event-driven: writing state directly works
for immediate UI reads but downstream consumers (inventory-awards, quest
updates, per-ref flags) only fire on dispatch. Symptom: your direct write
shows in the UI, but the "completion reward" never materializes.

Always search for the "check-and-notify" routine adjacent to the state
writer. For planet surveys it's `ID_97853`; it rebuilds the state into a
temp buffer, compares to a prev-count, and conditionally fires
`PlayerPlanetSurveyCompleteEvent`. Bulk writers should call this *after*
the writes finish, with a minimal context struct.

## Hooking

CommonLibSF provides `REL::Relocation` + trampoline helpers. A typical detour:

```cpp
struct Hook {
    static void thunk(RE::Actor* a) {
        spdlog::info("actor {:X}", a->GetFormID());
        func(a);
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

void Install() {
    REL::Relocation<std::uintptr_t> target{ REL::ID(142857), 0x42 };
    auto& trampoline = SFSE::GetTrampoline();
    Hook::func = trampoline.write_call<5>(target.address(), Hook::thunk);
}
```

Always allocate trampoline size up front in `PostLoad`: `trampoline.create(64)`.
