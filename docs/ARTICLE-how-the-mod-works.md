# How Complete Planet Survey Works

*For players who know their way around a load order, and modders who want to know
what this thing actually touches. Short version: the mod doesn't invent a survey
system — it finds every place the game already records survey progress and fills
those in the same way the engine would, just without making you fly there.*

---

## The three files, and how a command reaches the code

The mod is the classic SFSE sandwich:

- **`CompletePlanetSurvey.dll`** — an SFSE plugin (C++, CommonLibSF). All the
  real work happens here. Engine functions are addressed through the **Address
  Library**, so the same DLL keeps working across game patches as long as a
  matching address library exists.
- **`CompletePlanetSurvey.esm`** — a tiny master that exists mostly to add two
  toggles to the game's own **Settings → Gameplay** menu ("Hand Scanner" and
  "Orbital Scanner"). They're ordinary `GPOF` option records, so the game
  renders, saves and restores them like any vanilla setting — the mod just reads
  them.
- **Papyrus scripts** — the console-facing layer. When you type
  `cgf "CompletePlanetSurveyQuest.CompletePlanet" "all"`, you're using the
  game's own *CallGlobalFunction* console command to invoke a Papyrus function,
  which immediately calls into native functions the DLL registered with the
  script VM. No hotkeys, no injected UI — the console *is* the interface.

One quirk worth knowing: the mod's scripts are *formless* (not attached to any
quest), and Starfield's VM drops the script types for formless scripts when you
quit to the main menu — which silently unbinds their native functions. The DLL
watches for that and re-registers them each session, which is why the commands
survive a Main Menu → load round trip.

---

## What "survey data" actually is

There is no single "surveyed = true" flag. When you survey a planet normally,
the game scatters the progress across several stores, and *all* of them have to
agree before the UI says 100%:

1. **A per-save knowledge database** (`BSGalaxy::PlayerKnowledge`, reached
   through a singleton manager). For each planet it holds a per-species entry
   with three things the mod cares about:
   - a **scan-flag byte** — this alone is what makes a creature's outline
     **green** instead of blue,
   - a **percent byte** — what the survey % math and category counters read,
   - a **marker list** — the rows the hand scanner shows for a scanned species
     (genetics, reproduction, temperament, biomes, resource, …).
2. **Attribute "known" bits** on the planet's data record — water, atmosphere,
   traits, resources.
3. **The trait-known list** — planet traits you've discovered.
4. **Misc statistics** — the character sheet's *Planets Scanned*, *Flora/Fauna
   Fully Scanned*, *Unique Creatures Scanned* counters.

The crucial property (confirmed by testing, and the reason the mod can work at
all): the green outline is **saved data, not paint**. Creatures spawn fresh
every time you land, and each one checks the knowledge database to decide its
outline colour. Complete a planet remotely, restart the game, land there for the
first time — everything spawns already green, because the *fact* was written to
the save.

---

## The vanilla systems the mod piggybacks on

The design rule throughout: **prefer the engine's own routine over a hand-rolled
write**, and where a raw write is unavoidable, write exactly the bytes the
engine's real scan writes.

**The engine's own "fully survey this planet" call.** For barren worlds (no
flora/fauna — most of the ~1,798 bodies), the mod calls the same internal
routine a real scan funnels into. That one call creates the knowledge entry,
marks the survey complete, drops the **Survey Data** slate into your inventory,
and grants the survey XP — because it *is* the vanilla code path, not an
imitation of it.

**The survey-notify convergence point.** Every survey change in the game —
hand scan, orbital scan, quest discovery — converges on one internal
check-and-notify function. The two Settings toggles are implemented as
crash-safe hooks on two of its *call sites*: the on-surface hand-scanner site
(Hand Scanner toggle) and the star-map scan site, filtered to the
"scan-level-changed" reason (Orbital Scanner toggle). The mod doesn't replace
the function; it observes the call, notes which planet was scanned, and queues a
full completion for that body. After an orbital completion it also re-invokes
the engine's own star-map panel-populate call, so the info panel repaints to
100% in place.

**The vanilla survey quest, for trait scan objects.** Those "Unknown Feature —
0 of N scanned" landmarks are owned by the game's survey quest (`SQ_Parent`).
Rather than poking their internals, the mod drives the quest's own Papyrus API:
set the location's scan-count actor value, mark the references scanned, and call
the quest's `DiscoverMatchingPlanetTraits` — the same calls the quest makes for
itself. (These objects have to be loaded, so this part runs for the planet
you're standing on; remote worlds resolve on arrival because the trait is
already known.)

**The stats API.** The character-sheet counters are ordinary misc stats, so the
mod increments them through the game's own stat machinery — which also means you
can inspect or adjust them yourself with vanilla `GetPCMiscStat` /
`ModPCMiscStat` console commands.

---

## The two genuinely hard problems

**1. The game only knows what lives on planets you've visited.** A planet's
flora/fauna table is materialised lazily on landing; for a never-visited world
it's empty in memory. You cannot ask the running game "what lives on
Va'ruun'kai?" if you've never been there.

The answer is to go around the engine entirely: the DLL contains an **offline
plugin reader** that, once per session, opens **every plugin in your load
order** and parses the authored planet records (`PNDT`, with their
zlib-compressed `PPBD` per-biome blocks) straight off disk. That yields the
complete *species → planets* map with no visiting required.

Since v1.5.0 this reader is load-order aware in the full sense modders will
recognise: it reads the actual load order from the running game's data handler,
handles all three plugin classes (**full**, **medium** `FD`-prefix, and
**small/light** `FE`-prefix masters), remaps every file-local FormID to its
runtime value, and applies the *last-loaded-wins* override rule — so a DLC
override of a base-game planet, or base-game flora placed on a DLC world,
resolves exactly as the engine resolves it. That's what fixed Shattered Space:
Va'ruun'kai's roster lives in `ShatteredSpace.esm`, which the reader previously
never opened.

**2. Knowing what a scanned species *shows*.** Green isn't just the flag — a
properly scanned species also has its marker rows (genetics, reproduction,
temperament, …). Which markers a species gets is not stored per species; the
engine computes it at scan time by evaluating **condition lists** (`CTDA`) on
two FormList "catalogs" (`HandScannerPlantKeywords` /
`HandScannerActorKeywords`), against inputs like the planet's trait keywords and
the creature's granted keywords. The mod ports that evaluator and runs it
**offline against the same authored data** — the catalogs, the condition forms,
the OMOD keyword grants, the flora reproduction property. The output is
byte-identical to what the engine writes for a real scan (validated against
in-game ground truth), which is why a remotely-completed species looks
indistinguishable from a hand-scanned one.

With both problems solved, the remote green is just three writes per
(planet, species), keyed the same way the engine keys them: scan-flag byte,
percent byte, marker list. Re-runs check the flag first and skip anything
already complete, so repeat commands are true no-ops — no stat inflation, no
double XP.

---

## Ordering, pacing and safety

A full-galaxy run is a native sweep (all ~1,798 bodies, near-instant) followed
by a Papyrus finalize pass that handles traits and the handful of entries the
engine creates asynchronously — script passes deliberately yield across frames
so the game never hitches. Life worlds get their per-species green writes; only
worlds that genuinely have no flora/fauna take the ref-free "fully survey"
shortcut, so a living world is never falsely stamped 100% with unscanned
species (the bug DLC worlds used to hit before the reader saw them).

Because a fault in a native plugin crashes the whole game, the DLL is written
defensively: engine pointers are null-checked before every dereference,
faultable engine calls run inside try/catch, loops and allocations are bounded,
and if the load-order read ever fails the reader degrades to base-game-only
rather than guessing. Everything logs to
`Documents\My Games\Starfield\SFSE\Logs\CompletePlanetSurvey.log` if you want to
watch it work.

---

## What it doesn't do

No save-file editing, no new quests or scenes, no changes to vanilla records —
the ESM only *adds* two option records. Nothing runs unless you invoke a console
command or enable one of the two opt-in toggles. And the one honest side effect:
completing planets through the engine's own survey call grants the XP a real
survey would, so a whole-galaxy run is a large level jump — that's the vanilla
reward code firing, not a bonus the mod adds.
