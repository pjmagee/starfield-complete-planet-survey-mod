# Deployment State Awareness

**The README is not the truth. The filesystem is the truth.** A Starfield install can have ESMs, SFSE DLLs, INI tweaks, Plugins.txt entries, and ContentCatalog registrations all dropped over months of iteration. None of them go away just because the project's docs claim a different architecture. Verify before claiming, before promising "no conflict", and before declaring a session done.

This is the missed-step that produced "frankenstein" states like *the SFSE plugin is the only thing doing X* when in fact a stale ESM was still active alongside it.

---

## When to verify

Always run the verification checklist below at these moments:

| Moment | Why |
| --- | --- |
| **Start of any modding session** | Confirm the baseline before changing anything. |
| **Before claiming "X is the only thing doing Y"** | Documentation drift is the default. |
| **Before / after architecture pivots** (ESM → SFSE, hybrid → pure) | Old layer doesn't auto-uninstall when the new one ships. |
| **When debugging unexplained behavior** | Stale plugin / cached file / leftover override is a top-3 root cause. |
| **Before declaring a session complete** | "I rewrote the README to say pure SFSE" ≠ "I removed the ESM." |

If a previous session's notes / memory claim a deployment state, **verify it instead of trusting it** — sessions get interrupted, files get reverted, mod managers (Vortex, MO2) reshuffle things between runs.

---

## The verification checklist

Run these commands (or their PowerShell equivalents) before claiming any deployment-state fact. The paths assume a default Steam install at `E:\SteamLibrary\steamapps\common\Starfield` and Documents under OneDrive — adjust per machine.

### 1. Game install — what files exist

```bash
# Active ESMs/ESPs in Data\
ls -la "E:/SteamLibrary/steamapps/common/Starfield/Data/" | grep -iE "\.es[mlp]$|\.bak"

# Active SFSE plugins
ls -la "E:/SteamLibrary/steamapps/common/Starfield/Data/SFSE/Plugins/"

# Loose-files override directory (anything here wins over BA2 archives)
ls -la "E:/SteamLibrary/steamapps/common/Starfield/Data/Scripts/" 2>/dev/null | head -20
```

`.bak`, `.bak2`, `.bak3` files mean prior iterations exist — note them but they don't load.

### 2. Plugin load order — what the engine actually loads

`Plugins.txt` lives in **`%LOCALAPPDATA%\Starfield\Plugins.txt`**, NOT in `Documents\My Games\Starfield`. Common mistake.

```bash
grep -inE "^[^#]" "/c/Users/$USER/AppData/Local/Starfield/Plugins.txt"
```

Lines starting with `*` are **enabled**. Lines starting with `#` are commented out. Plain lines without `*` are present-but-not-loaded. Ordering is top-to-bottom = lowest-priority-to-highest (later overrides earlier). Verify the plugin you think is active actually has the `*` prefix.

### 3. ContentCatalog registration — Creation-shop gate

Post-CK Starfield silently discards plugins absent from `ContentCatalog.txt`. Check:

```bash
grep -inE "freecrafting|TM_[a-f0-9-]{36}" "/c/Users/$USER/AppData/Local/Starfield/ContentCatalog.txt"
```

Each registered plugin has a `TM_<uuid>` block with `"Files" : [ "Foo.esm" ]`. If your plugin is in `Plugins.txt` but missing from `ContentCatalog.txt`, the engine ignores it — looks loaded, isn't.

### 4. INI overrides — silent behavior changes

```bash
cat "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/StarfieldCustom.ini" 2>/dev/null
cat "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/Starfield.ini"        2>/dev/null
```

Watch especially for `bFreeItemCrafting=1`, `[Workshop] bBudgetEnabled=0`, archive-invalidation flags — any of these can mask or amplify mod effects and confuse "is my hook even running?" diagnosis. The user-facing Documents path may be under OneDrive (`[Environment]::GetFolderPath("MyDocuments")` on Windows resolves it correctly).

### 5. SFSE plugin output — proof of life

```bash
ls -la "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/SFSE/Logs/"
cat   "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/SFSE/Logs/<YourPlugin>.log"
cat   "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/SFSE/Logs/sfse.txt"
```

If your plugin has a `.log` file with hook-install lines, the DLL loaded and ran. If the file is missing or empty, the plugin didn't initialize — could be wrong runtime version, missing Address Library, dependency mismatch. `sfse.txt` lists every plugin SFSE loaded plus rejection reasons.

### 6. Crash artifacts — ignore the .dmp, look for CrashLogger

```bash
# Raw minidumps Starfield drops on crash (large, unreadable without WinDbg)
ls -la "E:/SteamLibrary/steamapps/common/Starfield/"*.dmp 2>/dev/null

# CrashLoggerSF (Nexus 3273) human-readable traces — install proactively
ls -la "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/SFSE/Logs/" | grep -i crash
```

`.dmp` files are 140 MB+ each and require WinDbg + symbols to read. **Recommend [CrashLoggerSF](https://www.nexusmods.com/starfield/mods/3273) at the start of any reverse-engineering session** — it produces a `crash-YYYYMMDD-HHMMSS.log` with the offending module + offset and a stack trace. Without it, post-crash diagnosis is guesswork.

---

## Architecture-pivot cleanup

When pivoting from one layer to another (e.g. ESM → pure SFSE), the deprecated layer's footprint must be **actively removed**, not just stop-being-edited. Cleanup ritual when retiring an ESM:

1. **Inventory** — `ls Data/*.esm`, grep `Plugins.txt`, grep `ContentCatalog.txt`. Note every reference.
2. **Confirm with the user** before deleting deployed files — destructive ops on a game install always need explicit go-ahead.
3. **Backup if not already** — keep one `.bak` so a rollback is a copy away (but don't ship `.bak`s in the FOMOD).
4. **Remove**:
   - Delete or move `Data\YourMod.esm`
   - Delete (don't just comment) the line in `%LOCALAPPDATA%\Starfield\Plugins.txt`
   - Remove the `TM_<uuid>` block (3 lines: opening, `"Files"`, closing) from `%LOCALAPPDATA%\Starfield\ContentCatalog.txt`
5. **Re-verify** by re-running the checklist. Confirm the file is gone, the line is gone, the registration is gone.
6. **Update memory / project_status memory** — ESM-state, SFSE-state, what's deployed and what isn't. Stale memory caused the original drift.

When pivoting between SFSE plugin builds (rebuild + redeploy), Starfield holds the DLL open while running — `deploy.bat` will error with "file in use." Always confirm Starfield is fully exited (not alt-tabbed) before redeploy.

---

## Anti-patterns to never repeat

- **"I rewrote the README to say pure SFSE."** — README claims and `Plugins.txt` reality drift independently. Update both.
- **"The ESM hasn't been touched in this session, so it's not part of the picture."** — If it's still in Data\ + Plugins.txt + ContentCatalog, it's running. Time-since-edit is irrelevant.
- **"The previous session's notes say only the DLL is deployed."** — Sessions die. Filesystems persist. Verify.
- **Trusting documentation over filesystem state when claiming an architectural fact** — backwards. Filesystem first, then update docs.
- **Declaring "no conflict" without checking** — answer either "yes, I checked, here's the state" or "I haven't checked yet."
- **Deleting deployed files without explicit user authorization** — destructive ops on the user's game install always confirm first, even when you're certain the file is stale.

---

## TL;DR for the impatient

Before claiming any deployment-state fact, run:

```bash
ls "E:/SteamLibrary/steamapps/common/Starfield/Data/"*.esm \
   "E:/SteamLibrary/steamapps/common/Starfield/Data/SFSE/Plugins/"*.dll
grep -E "^\*" "/c/Users/$USER/AppData/Local/Starfield/Plugins.txt"
grep "TM_"   "/c/Users/$USER/AppData/Local/Starfield/ContentCatalog.txt" | head
ls "/c/Users/$USER/OneDrive/Documents/My Games/Starfield/SFSE/Logs/"
```

That's the ground truth. Anything else is hearsay.
