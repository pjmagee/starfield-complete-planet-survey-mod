# SFSE — Starfield Script Extender

Upstream: https://github.com/ianpatt/sfse
Distribution build: https://www.nexusmods.com/starfield/mods/7589

SFSE is a DLL loader and runtime service layer that lets native plugins (`.dll`) hook into Starfield.exe. It provides:

- Process injection and plugin loading (`Data/SFSE/Plugins/*.dll`)
- A messaging bus (load/save game, data loaded events)
- Papyrus extension registration (expose native C++ to `.psc` scripts)
- Trampoline allocators and branch/call detour helpers
- Version metadata so plugins can refuse to load on incompatible runtimes

## Plugin layout (minimal)

```cpp
#include "sfse/PluginAPI.h"

extern "C" __declspec(dllexport) SFSEPluginVersionData SFSEPlugin_Version = {
    SFSEPluginVersionData::kVersion,
    MAKE_VERSION(1, 0, 0),                   // plugin version
    "MyPlugin",
    "Author",
    SFSEPluginVersionData::kAddressIndependence_AddressLibrary,
    SFSEPluginVersionData::kStructureIndependence_NoStructs,
    { RUNTIME_VERSION_1_16_236_0, 0 },       // compatible runtimes
    0, 0, 0, 0
};

extern "C" __declspec(dllexport) bool SFSEPlugin_Load(const SFSEInterface* sfse) {
    // register Papyrus functions, install hooks, etc.
    return true;
}
```

## Addressing modes

SFSE supports two ways for plugins to locate game code/data:

1. **Address Library** (preferred) — stable IDs mapped to per-version offsets. The database is loaded from `Data/SFSE/Plugins/versionlib-*-*.bin` shipped by Nexus mod 3256. You look up `REL::ID(12345)` and get the right absolute address for whatever runtime is currently loaded.
2. **Signature scanning** — pattern-match bytes in `.text`. Fragile across updates; use only for identifiers not yet in the Address Library.

Never hardcode a raw offset. A hardcoded `0x1400XXXXX` breaks the moment the game patches.

## Plugin load lifecycle

1. SFSE DLL injected into Starfield.exe early
2. SFSE reads `Data/SFSE/Plugins/*.dll`, checks `SFSEPlugin_Version`
3. Mismatched runtime versions -> plugin rejected with a log line in `My Games/Starfield/SFSE/Logs/`
4. Compatible plugins get `SFSEPlugin_Load` called
5. Plugins register for messages: `kMessage_PostLoad`, `kMessage_DataLoaded`, `kMessage_PreLoadGame`, `kMessage_PostLoadGame`, `kMessage_SaveGame`
6. Do Papyrus registration on `kMessage_PostLoad` (or via `GetPapyrusInterface()->Register(...)`)

## Logging

Use SFSE's log facility or bring your own (spdlog is common). Logs go to `%USERPROFILE%\Documents\My Games\Starfield\SFSE\Logs\<plugin>.log`. Never log to stdout — there is no console.

## Tips

- Build with `/MT` or match CRT to whatever CommonLibSF you use.
- Keep `SFSEPlugin_Version` in a `.cpp` (not a header) — exactly one definition per DLL.
- For runtime detours, allocate trampolines close to the target (`REL::SafeWrite` / CommonLibSF's `Relocation<>`).
