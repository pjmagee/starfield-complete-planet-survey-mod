# CommonLibSF

A community-maintained C++ library that wraps SFSE + reverse-engineered Starfield types into ergonomic `RE::` namespaces — analogous to CommonLibSSE/CommonLibF4 for Skyrim/Fallout 4.

Typical vendored location: `extern/CommonLibSF/`.

## What you get

- `RE::` types: `TESForm`, `TESDataHandler`, `Actor`, `BGSResource`, `BGSKeyword`, `BSTArray`, `BSAutoReadLock`, ...
- Singletons: `RE::TESDataHandler::GetSingleton()`, `RE::PlayerCharacter::GetSingleton()`, etc.
- `REL::Relocation<T>` / `REL::ID` for Address Library lookups
- Papyrus helpers: `BSScript::IVirtualMachine`, `BindNativeMethod`, `RegisterFunction`
- `SFSE::Init`, message handlers, trampoline wrappers

## Registering a Papyrus native function

```cpp
namespace Papyrus {
    std::int32_t GetSurveyedResourceCount(std::monostate, RE::TESObjectCELL* a_cell) {
        // ... native logic using RE:: types ...
        return count;
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
        vm->BindNativeMethod("MyScript", "GetSurveyedResourceCount",
                             GetSurveyedResourceCount, true);
        return true;
    }
}

SFSEPluginLoad(const SFSE::LoadInterface* sfse) {
    SFSE::Init(sfse);
    SFSE::GetPapyrusInterface()->Register(Papyrus::RegisterFunctions);
    return true;
}
```

The Papyrus side just declares:

```papyrus
Int Function GetSurveyedResourceCount(ObjectReference akCell) native global
```

`std::monostate` as the first arg means "global native" (no `self`). Use `RE::StaticFunctionTag*` equivalently in older patterns.

## Address Library usage

```cpp
// Wrap a function pointer at a stable ID
static REL::Relocation<std::uint64_t(*)(RE::TESForm*)> GetResourceId{ REL::ID(142857) };

auto id = GetResourceId(form);
```

IDs come from the Address Library database. When the game updates, rebuild against the new database — your source stays the same.

## Build system

`xmake` is the common choice (see [xmake.lua](../../../xmake.lua) in this repo). Alternative: CMake with `find_package(CommonLibSF CONFIG REQUIRED)`. Pin CommonLibSF to a specific commit; breaking RE changes happen.

## Gotchas

- `BSTArray` is not `std::vector`; iteration is usually index-based.
- Lock form arrays before iterating: `const RE::BSAutoReadLock lock(formArray.lock);`
- `GetFormEditorID()` can return nullptr — always guard.
- Singleton getters may return nullptr during very early load; guard in `PostLoad`/`DataLoaded`, not `SFSEPlugin_Load`.
- Struct offsets are RE'd by hand; if a field looks wrong, check the current commit's header against Ghidra.
