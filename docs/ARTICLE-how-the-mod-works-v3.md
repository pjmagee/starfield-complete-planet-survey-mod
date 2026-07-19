# Complete Planet Survey — How It Works

A plain explanation of what the mod does, how it does it, and what it does not
do.

## What the mod is

Three files:

| File | What it is |
|---|---|
| `CompletePlanetSurvey.dll` | An SFSE plugin (C++). This is where all the work happens. |
| `CompletePlanetSurvey.esm` | A small plugin that adds two toggles ("Hand Scanner", "Orbital Scanner") to Settings → Gameplay. It adds two records and modifies nothing else. |
| Papyrus scripts | The console commands. They validate your input and call functions in the DLL. |

You use it either by typing a console command (for example
`cgf "CompletePlanetSurveyQuest.CompletePlanet" "all"`) or by enabling one of
the two toggles, after which scanning a planet (hand scanner on the ground, or
a scan on the star map) completes that planet's survey automatically.

## How survey progress is stored by the game

There is no single "surveyed" flag. When you survey normally, the game records
progress in several places, all inside your save:

- A **knowledge database**: for each planet, a per-species entry holding a
  scan flag (this is what makes an outline green instead of blue), a percent
  value, and the list of entries the scanner panel shows for that species
  (genetics, reproduction, temperament, and so on).
- **Attribute bits** on the planet (water, atmosphere, resources known).
- A **trait list** (which planet traits you have discovered).
- The **misc statistics** on your character sheet (Planets Scanned, Flora
  Fully Scanned, etc.).

A planet shows 100% surveyed when all of these agree. Creatures and plants are
spawned fresh every time you land; each one reads the knowledge database to
decide whether its outline is blue or green. The green is saved data, not
something attached to the creature.

## What the C++ does, at a high level

1. **At game startup**, the DLL reads the planet data out of your plugin files
   — every master in your load order (base game, DLC, Creations), not the
   running game — because the running game only knows a planet's species list
   after you have landed there. This gives it, for every planet, the exact
   list of flora and fauna that world was authored with. It also derives what
   the scanner panel would show for each species, by evaluating the same
   authored condition data the game itself evaluates during a real scan.
2. **When you run a command**, the DLL writes the survey state described above
   into the game's own in-memory bookkeeping, using the engine's own internal
   functions (located version-independently through the Address Library). For
   lifeless worlds it calls the engine's own "fully survey this planet"
   routine — the same one an orbital scan ends in — which is why you receive
   the Survey Data slate and survey XP. For living worlds it writes, per
   species, the same scan flag, percent, and panel entries a real hand scan
   would write. Traits are completed through the game's own survey quest.
3. **The two toggles** are implemented as hooks on the engine function that
   every scan already reports to. The mod observes "a planet was just
   scanned", waits for the scanner UI to close, then completes that planet the
   same way the command would. After a star-map scan it also refreshes the
   info panel using the game's own refresh call, so the % updates in place.
4. **Saving is untouched.** The mod writes into the same runtime data the game
   itself maintains; your next normal save persists it, exactly as if you had
   done the scans yourself.

The result is indistinguishable from having surveyed manually — verified by
comparing completed species against genuinely hand-scanned ones.

## What it does do

- Completes surveys: resources, traits, flora, fauna — per category, per
  planet, or galaxy-wide.
- Works on planets you have never visited, from wherever you are.
- Works on DLC and mod-added planets (v1.5.0 and later), by reading your full
  load order.
- Updates your character-sheet statistics through the game's stat system.
- Grants survey XP for each completed world — a side effect of using the
  game's own survey routine, not a bonus the mod adds. A full-galaxy run is a
  large level jump.
- Is safe to re-run: a second run detects the work is already done and does
  nothing (no double XP, no inflated statistics).

## What it does not do

- It does **not** edit your save file on disk. It writes through the running
  engine; the game saves normally.
- It does **not** spawn creatures, teleport you, load cells, or fast-travel.
  Everything is data writing.
- It does **not** modify any vanilla record. The ESM only adds two settings
  records, so it does not conflict with other mods.
- It does **not** run anything in the background. Nothing happens unless you
  type a command or have enabled a toggle and scanned something.
- It does **not** touch quests, NPCs, economy, or anything outside survey
  data.

## Common misconceptions

**"It just paints the outlines green."** No. It writes the same underlying
survey data a real scan writes. The green outline, the survey %, the scanner
panel details, the slate, and the statistics all follow from that data.

**"It must visit each planet in the background."** No. Nothing is visited or
loaded. Planet contents are read from the game's plugin files; completion is a
data write into the save's knowledge records.

**"If I uninstall the mod, the surveys revert."** No. The survey data is
ordinary vanilla save data. Remove the mod and your completed surveys remain,
exactly as if you had scanned everything by hand.

**"The XP means the mod is cheating extra rewards in."** The XP comes from the
game's own survey-completion routine, which the mod deliberately uses for
lifeless worlds. It is the standard reward for surveying; the mod adds nothing
on top.

**"It will keep working after any game patch."** Like every SFSE plugin, the
DLL requires a matching SFSE and Address Library for your exact game version.
After a game update, wait for both to update. The mod's own data (your
completed surveys) is never at risk — it lives in the save.

**"Console commands are risky / modify the game."** `cgf` is a built-in
Starfield console command that calls a script function. The usual Starfield
rule applies: using the console flags the session as modded (relevant to
achievements, as with any console use); the mod does not change that behavior
in either direction.

**"It needs an ESP/ESM patch for each DLC."** No. Since v1.5.0 the DLL reads
whatever masters your load order actually contains, using the game's own
load-order rules. New DLC or mod-added planets are picked up automatically.
