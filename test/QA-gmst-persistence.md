# QA: GMST save-persistence (uninstall hazard)

**GitHub issue:** [#15 — Verify GMST save-persistence (uninstall hazard)](https://github.com/pjmagee/starfield-complete-planet-survey-mod/issues/15)  
**Status:** Awaiting in-game test (user-only). This document records code inspection and the test protocol; no runtime result is claimed here.

---

## 1. Background and stakes

Complete Planet Survey sets two hand-scanner **game settings** (GMSTs) to `1` on every data load so a single scan counts as a full species completion (same idea as the Nexus “Instant Scan” mod). That behavior is intentional while the plugin is installed.

The risk: if `GameSettingCollection::SetSetting` writes those values into the **save file**, then **removing the DLL** does not restore vanilla thresholds. The player would keep “instant scan” behavior with no mod present and no in-mod revert path—an uninstall hazard.

### What the code does today

`Engine::ApplyInstantScanGameSettings` patches the two GMSTs via CommonLibSF’s `RE::GameSettingCollection::SetSetting` (string GMST name → `std::int32_t` value). There is **no** `REL::ID` hook for this path; it is a direct singleton call, not an engine-function offset bind.

```893:914:src/Main.cpp
    // Patch the scanner's per-species required-count GMSTs so each individual scan
    // counts as a full completion. Matches the "Instant Scan" mod's approach (Nexus
    // mods/759) — just two SetGS calls, no ESM, no CCR dependency.
    //
    // Without this: the engine requires N scans per species (N varies — often 6)
    // before marking the species complete. Our CompleteSurvey post-scan flag-flip
    // usually overrides that anyway by bumping the flag past any threshold, but
    // species that slip through our iteration (rare spawns, sub-biomes not returned
    // by GetBiomeFlora/GetBiomeActors) still cap at whatever natural scan count
    // the player reached. Setting the threshold to 1 closes that gap.
    void ApplyInstantScanGameSettings()
    {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (!settings)
        {
            spdlog::warn("ApplyInstantScanGameSettings: GameSettingCollection singleton null");
            return;
        }
        const bool animalOk = settings->SetSetting<std::int32_t>("iHandScannerAnimalCountBase", 1);
        const bool plantOk  = settings->SetSetting<std::int32_t>("iHandScannerPlantsCountBase", 1);
        spdlog::info("ApplyInstantScanGameSettings: animal={} plants={}", animalOk, plantOk);
    }
```

### Where it runs

The function is invoked once per session from the SFSE messaging listener when `kPostDataLoad` fires (after ESM sources are configured, Papyrus natives are registered, and scan hooks are installed):

```1971:1984:src/Main.cpp
    void MessageCallback(SFSE::MessagingInterface::Message* a_msg) noexcept
    {
        if (a_msg->type == SFSE::MessagingInterface::kPostDataLoad)
        {
            ConfigureEsmSources();  // must precede any Esm:: query (the parse is one-shot)
            Papyrus::Register();
            Hook::Install();
            Hook::InstallStarMapScanHook();     // galaxy-map planet scan → complete-that-planet
            Hook::InstallStarMapRefreshHook();  // capture the StarMap menu for the post-completion repaint
            Hook::InstallScanSweepPoller();
            Hook::InstallSessionReRegisterPoller();  // re-bind natives after main-menu -> load (formless scripts)
            Engine::ApplyInstantScanGameSettings();
            spdlog::info("CompletePlanetSurvey initialized");
        }
    }
```

The plugin registers only this listener in `SFSE_PLUGIN_LOAD`; it does not handle unload/teardown messages for GMST restore.

**Stakes summary:** Code **always** sets both GMSTs to `1` while the plugin loads. Whether that state **survives** save → uninstall → reload is unknown from the repository alone and must be measured in-game.

---

## 2. What we know from code (and prior notes)

| Topic | Finding |
|--------|---------|
| **How GMSTs are set** | `RE::GameSettingCollection::GetSingleton()` then `SetSetting<std::int32_t>("iHandScannerAnimalCountBase", 1)` and `SetSetting<std::int32_t>("iHandScannerPlantsCountBase", 1)`. Return values are logged at INFO (`animal=` / `plants=`). |
| **REL::ID / native hook?** | **No** for this feature. Instant-scan GMSTs are not applied via a scanned engine function ID; they use CommonLib’s game-setting API (console-equivalent to `setgs`). |
| **Restore / revert in plugin?** | **None.** Grep of `src/Main.cpp` shows no second call to reset these GMSTs, no `kMessage`-style teardown handler, and no pre-save hook that reverts values. Uninstall = stop calling `ApplyInstantScanGameSettings`; nothing in-mod writes vanilla defaults back. |
| **When values apply** | Every time `kPostDataLoad` runs with the DLL present (new game load / continue from main menu after SFSE init). |
| **`test/` prior notes** | `test/qa/QA_FINDINGS.md` does not mention GMST persistence. Offline harness (`test/build_validate.bat`, `ValidateMarkers.cpp`) does not touch game settings. |
| **`docs/` prior notes** | `docs/design-critique.md` § “Behavioral unknowns — needs in-game test” lists **GMST save-persistence** as an open item (stale line refs; logic matches current `ApplyInstantScanGameSettings` + `kPostDataLoad`). `docs/dead-code-audit.md` notes the function as live setup path, not dead code. README describes the behavior for players but does not document uninstall GMST risk or revert steps. |
| **`re/` vanilla defaults** | No checked-in dump names `iHandScannerAnimalCountBase` / `iHandScannerPlantsCountBase`. In-code comment: per-species required count **“N varies — often 6”** before species complete. **Treat vanilla numeric defaults as unverified in-repo** until measured with `getgs` on a save that never had this mod (see optional baseline below). |

**Log check (optional, with mod):** After load, `CompletePlanetSurvey.log` should contain  
`ApplyInstantScanGameSettings: animal=true plants=true` (or `false` if `SetSetting` failed).

---

## 3. In-game test protocol

**Prerequisites**

- SFSE + Address Library, game build matching the mod release (see `CHANGELOG.md`).
- Mod installed (`CompletePlanetSurvey.dll` under `Data/SFSE/Plugins/`, ESM enabled).
- Use a **test save** (not your only playthrough) in case persistence alters the file.
- Starfield **closed** before removing the DLL (deploy script refuses while the game is running for the same reason).

**Optional baseline (vanilla defaults)** — on a save or new character **without** this mod loaded, open console and record:

```text
getgs iHandScannerAnimalCountBase
getgs iHandScannerPlantsCountBase
```

Fill the “Vanilla baseline” row in the results table. If you skip this, note “unknown” and compare post-uninstall values only to `1` vs “not 1”.

### Steps

| Step | Action |
|------|--------|
| **A** | Install/enable Complete Planet Survey. Launch via `sfse_loader.exe`. |
| **B** | Load the test save (wait until fully in-game). `kPostDataLoad` should have run `ApplyInstantScanGameSettings`. |
| **C** | Open console (`~`). Confirm mod applied settings (expect `1` while mod is active): |
| | `getgs iHandScannerAnimalCountBase` |
| | `getgs iHandScannerPlantsCountBase` |
| **D** | Save the game (manual save or quicksave). Note save name / slot. |
| **E** | Exit to desktop. **Do not** load again with the mod still installed for the uninstall leg. |
| **F** | Disable/uninstall the plugin: remove or rename `Data/SFSE/Plugins/CompletePlanetSurvey.dll` (and optionally disable `CompletePlanetSurvey.esm` in your loader to mirror a full uninstall). |
| **G** | Launch **still via SFSE** (other mods may require it; SFSE without this DLL is the hazard scenario). |
| **H** | Load **the same save** from step D. |
| **I** | Console again: |
| | `getgs iHandScannerAnimalCountBase` |
| | `getgs iHandScannerPlantsCountBase` |
| **J** | Record both values in §5. Optionally verify gameplay: scan one fauna/flora species and note whether one scan completes the species (qualitative; GMST console values are the primary signal). |

**Pass/fail interpretation (for issue #15)**

- **No persistence hazard:** After step I, values match **vanilla baseline** (or typical non-`1` defaults), and behavior feels like vanilla multi-scan requirements.
- **Persistence hazard:** After step I, values are still **`1`** (or otherwise differ from baseline) with the DLL absent → save (or runtime state) retained mod-altered GMSTs.

---

## 4. Decision matrix

| Test outcome | Meaning | Candidate actions |
|--------------|---------|-------------------|
| **GMSTs do not persist** (reload without DLL → vanilla/baseline values) | Engine re-derives or reloads GMSTs from shipped defaults / INI; mod runtime patch did not bake into save. | Close issue #15: **no uninstall hazard** for these two settings. No code change required for persistence; keep current `kPostDataLoad` apply. |
| **GMSTs persist** (still `1` without DLL) | Save or session state carries altered game settings; uninstall leaves permanent instant-scan behavior. | **(a) Plugin revert:** On teardown / before save, set GMSTs back to vanilla defaults (would need known default integers and a reliable SFSE message—e.g. pre-save or exit). **(b) Documented uninstall:** README + Nexus uninstall section with console `setgs` to restore defaults (values from baseline step). |

### Recommendation (if persistence is confirmed)

Prefer **(b) documented manual uninstall** first, optionally followed by **(a)** only if a **proven** SFSE callback fires reliably on every save and quit path.

**Rationale**

- Teardown / pre-save hooks in Bethesda SFSE plugins are **not guaranteed** on all exit paths (hard quit, crash, alt-F4). A missed revert still poisons the save.
- Documented `setgs` steps are **zero implementation risk**, work even if the DLL is already deleted, and match how players already fix INI/GMST issues in Bethesda games.
- If defaults are confirmed via baseline `getgs`, document exact `setgs iHandScannerAnimalCountBase <n>` / `setgs iHandScannerPlantsCountBase <n>` (use measured `n`, not guessed `6`).
- **(a)** remains a nice-to-have if a maintainer later identifies a dependable `kSaveGame` / equivalent message and vanilla constants are pinned in code or docs.

If persistence is **not** confirmed, avoid adding uninstall GMST steps to player docs (noise and wrong default numbers).

---

## 5. Results (user fills after test)

| Field | Value |
|--------|--------|
| **Tester** | |
| **Date** | |
| **Game build / mod version** | |
| **Save used** | |
| **Vanilla baseline — `iHandScannerAnimalCountBase`** (no mod; optional) | |
| **Vanilla baseline — `iHandScannerPlantsCountBase`** (no mod; optional) | |
| **With mod, after load (step C) — animal** | |
| **With mod, after load (step C) — plants** | |
| **After uninstall + reload (step I) — animal** | |
| **After uninstall + reload (step I) — plants** | |
| **Persisted?** (`1` without DLL = yes hazard) | |
| **Gameplay spot-check (optional)** | |
| **Issue #15 disposition** | e.g. close “no hazard” / open “document setgs” / open “implement revert hook” |
| **Notes** | |

---

## References

- `src/Main.cpp` — `ApplyInstantScanGameSettings`, `MessageCallback` / `kPostDataLoad`
- `docs/design-critique.md` — behavioral unknown: GMST save-persistence
- README — Instant Scan equivalence ([Nexus 759](https://www.nexusmods.com/starfield/mods/759))