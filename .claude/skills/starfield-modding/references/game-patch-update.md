# Updating the Mod for a New Starfield Patch

The playbook for getting Complete Planet Survey (or any address-library SFSE
plugin) working again after Bethesda ships a game update. Written from the
1.16.242 → 1.16.244 update, which is the canonical worked example below.

**Golden rule: the filesystem and the SFSE log are the truth.** Don't infer
state from memory or the README — verify what's actually deployed and what the
loader actually did.

---

## The failure cascade after a patch (and what each layer means)

A game patch breaks address-library plugins in up to three distinct stages.
Diagnose in this order — each has a different fix and a different owner:

| # | Symptom | Cause | Owner / fix |
|---|---|---|---|
| 1 | `plugin … disabled, address library needs to be updated` in `sfse.txt` | The `versionlib-<build>.bin` for the running build isn't deployed | meh321's Address Library (Nexus 3256) — install/deploy the new "All in one"; **not** a rebuild |
| 2 | Startup popup: `REL/IDDB.cpp: Failed to find offset for Address Library ID! Invalid ID: <n>` | A CommonLibSF-resolved function's Address Library **ID moved** in the new build | Bump the CommonLibSF submodule to a commit that has the corrected ID; **rebuild** |
| 3 | Loads & runs, but behaviour is wrong / partial | A **hardcoded struct offset** (not protected by Address Library) moved | Re-RE that one struct on the new build (Ghidra); update the `constexpr` |

Stage 1 is the most common and is pure "wait for / deploy meh321's update."
Stages 2 and 3 require repo changes. **Stages 1 and 2 are independent** — you
can clear stage 1 and immediately hit stage 2.

Tell-tale: if *several unrelated* address-library mods fail **identically**,
it's the shared versionlib (stage 1), never any one DLL. See
`reference_sfse_addrlib_release_skew` in memory.

---

## Step-by-step

### 0. Verify deployment state (always first)

- Game build: `(Get-Item "<game>\Starfield.exe").VersionInfo.ProductVersion`
- SFSE present for that build: `sfse_<build>.dll` in the game root.
- versionlib present for that build: `versionlib-<build>-0.bin` in
  `Data\SFSE\Plugins\`. **If missing, that's stage 1** — and check the Vortex
  download archive (`%APPDATA%\Vortex\downloads\starfield\`); the new "All in
  one" is often already downloaded but not deployed.
- Last loader verdict: read `…\My Games\Starfield\SFSE\Logs\sfse.txt` —
  `disabled, address library needs to be updated` confirms stage 1.

### 1. Deploy the matching versionlib (stage 1)

Install the latest Address Library "All in one" via Vortex and **deploy**, then
confirm `versionlib-<build>-0.bin` exists in the game `Data\SFSE\Plugins\`. To
unblock fast without Vortex, the `.bin` can be extracted straight from the AiO
zip into that folder (additive, reversible) — but still install via Vortex for
clean tracking.

This mod is version-independent (`UsesAddressLibrary(true)` +
`HasNoStructUse(true)`) so SFSE bypasses the version whitelist — **no rebuild is
needed for stage 1 alone.** The existing shipped DLL will load once the
versionlib is present, *unless* you also hit stage 2.

### 2. If you get the IDDB popup, bump CommonLibSF (stage 2)

1. Identify the ID owner: `grep -rn "<id>" extern/CommonLibSF/include`. The popup
   ID is a CommonLibSF-internal `REL::ID`, not one of ours. (1.16.244 example:
   `139352` = `BSStringPool::GetEntry`, hit on the first `BSFixedString`
   constructed from a `const char*` — i.e. immediately.)
2. Check upstream for the fix — the maintainers update `IDs.h` per build:
   ```bash
   git -C extern/CommonLibSF fetch origin
   git -C extern/CommonLibSF log --oneline HEAD..origin/main
   ```
   Look for commits like `New BSStringPool … IDs` and `feat: add runtime <build>`.
   (1.16.244: `GetEntry 139352 → 1186742`, `GetEntryW 139354 → 1186743`, plus
   `RUNTIME_LATEST = 1.16.244`.)
3. Bump the submodule **properly** (pinned by SHA; the nested submodule is the
   step people forget):
   ```bash
   git -C extern/CommonLibSF checkout <new-sha>          # or origin/main
   git -C extern/CommonLibSF submodule update --init --recursive   # sync nested commonlib-shared
   git add extern/CommonLibSF                            # stage the new gitlink
   git submodule status --recursive                     # verify: no +/- prefixes
   ```
   CI consumes this via `actions/checkout … submodules: recursive` — the
   committed SHA *is* the dependency version; nothing else to bump.

### 3. Rebuild and deploy

- `build.bat` — xmake build (loads vcvarsall so dependency builds find MSVC).
  Output: `build\windows\x64\releasedbg\CompletePlanetSurvey.dll`. If the build
  dies with an *empty* error after a VS update, it's the stale-toolchain-cache
  footgun — see `tooling_xmake_msvc_cache` in memory.
- `deploy.bat` — compiles Papyrus, copies DLL+ESM+PEX, manages `plugins.txt`.
  **It refuses to run while Starfield is open** (the DLL/ESM are locked and a
  partial copy silently leaves a stale DLL loaded — this bit us twice). Fully
  exit the game first; verify the deployed DLL timestamp matches the build.

### 4. Validate — see the test checklist below

### 5. Ship

- CHANGELOG `[x.y.z]` entry: record the **exact game + SFSE build** tested
  (the changelog policy requires it), name the CommonLibSF bump and the moved
  ID, and mark it a **PATCH** (compatibility recompile — the mod's own contract
  didn't change).
- Commit, fast-forward `main`, tag `vX.Y.Z`, push the tag → CI builds,
  creates the GitHub release, and uploads to Nexus.
- **Only record a build as "tested" after you've watched it load and complete
  in-game** (changelog + memory rule).

---

## Test checklist for the next SF update

Run all of these before tagging. The mod has per-stage diagnostic logging in
`CompletePlanetSurvey.log` — one `CompleteSurvey:` block tells you which stage
broke (see the reading guide below), so capture the log for each scenario.

**Load & init**
- [ ] No `REL/IDDB.cpp` popup at launch.
- [ ] `sfse.txt` shows the plugin **loaded**, not `disabled`.
- [ ] `CompletePlanetSurvey.log` shows `CompletePlanetSurvey initialized`,
      `ScanHook: installed at call-site …`, and `ApplyInstantScanGameSettings`.

**Trigger paths** (the bug that hid behind "works on the command" — the console
command bypasses the hook, toggle, and poller, so it is **not** a sufficient
test on its own):
- [ ] Console: `cgf "CompletePlanetSurveyQuest.CompleteSurvey"` → 100%.
- [ ] Manual **resource** scan (toggle ON, planet <100%) → auto-completes;
      log shows `Poller: dispatched CompleteSurvey (scanner closed)`.
- [ ] Manual **flora** scan → auto-completes.
- [ ] Manual **fauna/creature** scan → auto-completes (exercises the
      `ID_83008 → ID_52160` route, distinct from flora/resource).

**Toggle behaviour**
- [ ] Toggle OFF → a scan does **not** auto-complete (no `Poller: dispatched`).
- [ ] Settings → Gameplay still shows the "Auto-Complete Survey on Scan" entry
      (GPOG/GPOF intact).

**Breadth** (the failure mode is data-dependent, so test several planets):
- [ ] At least 3–4 *different* planets reach 100%, including a fresh low-% one.
- [ ] Survey Data slate drops into inventory on completion.

**Read the diagnostic log per planet** — every stage should be healthy:
- `aggregator spans uint0/uint1/ptr0/ptr1` — all populated; a lone `0`
  alongside populated siblings ⇒ that span's offset moved (stage 3).
- `seen=N marked=N` — equal; `seen > marked` ⇒ DB lookup offset (`0x8B0`/`0x268`)
  is off; `seen=0` ⇒ aggregator returned nothing.
- `aggregated/flora/fauna/other/noform/kept` — `noform>0` ⇒ `LookupByID`
  (CommonLibSF) broke; `other` is expected (resources/traits, marked not spawned).
- `SpawnAndScan total/spawned/noForm/placeFail` — `placeFail>0` ⇒ some species'
  base form can't be `PlaceAtMe`'d, so that biome won't complete.
- `CompleteSurvey … survey=X% (was Y%)` — X should be 100.

If every stage is healthy but completion is still partial on one specific
planet, it's a data edge (an unplaceable species), not a version break — file
it as a known limitation, don't block the patch release on it.
