# How Complete Planet Survey Works

*A plain-English tour of what actually happens when you run the galaxy command —
with the real native functions and memory structures it leans on. No prior
modding knowledge assumed; the “under the hood” call-outs are for the curious.*

---

## The problem this mod solves

In Starfield, “surveying” a planet means physically flying there and scanning
every plant, animal, resource and trait until the planet reaches 100%. There are
roughly **1,798 planets and moons**. The game ships **no** “complete all surveys”
button, and **no** scripting function that does it.

Worse, survey progress isn’t one tidy number you can set. It’s a **scattered pile
of flags and bytes** the engine keeps per planet, plus entries in a hidden
per-save database. So this mod does something the game never intended: it reaches
into the engine’s own survey bookkeeping and fills it in **for every planet at
once, from wherever you happen to be standing.**

---

## What “a completed survey” really is

When you survey a planet normally, the game records your progress in two places:

1. **The planet’s data record** (`BGSPlanet::PlanetData`, living in memory) — the
   attribute “known” bits and the per-species scan-flag / percentage bytes.
2. **A per-save knowledge database** (`BSGalaxy::PlayerKnowledge`) — the
   trait-known list, and the **per-type “scanned species” tree** that the scanner
   outline reads from.

> **Under the hood.** The knowledge database is reached through a singleton
> manager (`ID_126578`). Survey scan-state is stored under an internal tag
> (discriminator `938333`); each planet’s data record points at its *surface
> tree* at offset `+0x38`. “100% surveyed” means all of the above are set — there
> is no single shortcut byte, so the mod has to set the **right combination**, the
> way the engine itself would.

---

## What blue and green mean — and why it’s *data*, not *paint*

First, the plain in-game meaning. When you look at a plant or creature through the
hand scanner, the colour of its outline tells you its scan status:

- **Blue** = **not yet scanned.** The game is telling you *“you still need to scan
  this to learn about it.”*
- **Green** = **already scanned.** You know this species here; there’s nothing
  left to learn.

So “make the flora/fauna green” really means *“mark every species as already
scanned.”* And here’s the part that trips people up: that green is **not a colour
applied to a creature** standing in front of you. It’s a **saved, per-(planet,
species) fact** in the knowledge database: *“on this planet, this species has been
scanned.”*

When you later land on a planet, the game spawns its wildlife **fresh**, and each
animal **checks that saved fact to decide its outline colour** — blue if the fact
isn’t set, green if it is. That’s why — confirmed in testing — you can complete a
planet, restart the entire game, fly there for the *first time*, and the creatures
spawn **already green**. The scan status lives in the save, not on any object.

This is the key that makes the whole mod possible: we **don’t** need a planet
loaded to make its creatures green. We only need to write the saved fact. Nothing
is ever spawned *on* a remote planet — that can’t be done, and it doesn’t need to
be.

---

## The two things that make “all planets at once” genuinely hard

**1. The engine ties completion to the planet you’re standing on.**
The normal “you finished scanning this” routine figures out *which* planet to
credit from your current location (`ID_52188` resolves your position). Stand on
Jemison and it only ever credits Jemison. To complete a *different* planet, we
have to call the deeper writers and **hand them the target planet explicitly**,
bypassing that location lookup.

**2. A planet’s species list can’t be read from memory unless you’ve been there.**
The list of which plants and animals live on a planet is materialised **lazily**,
only when you land and the cell loads. For a never-visited planet, that table is
empty in memory — so we can’t ask the running game “what lives here?”

> **Under the hood.** The per-biome species data (`CTPerBiomeData`, tag `938158`)
> and the spatial overlay (`CTPlanetOverlayData`, tag `938335`) are both populated
> on cell-load and stay empty for un-landed worlds. Chasing them through the
> surface tree’s opaque tile encoding was a dead end.

**The fix for #2: read the species straight from the game files.** Every planet’s
flora/fauna roster is *authored* on its plugin’s `PNDT` records (`PPBD`
sub-records) — `Starfield.esm` for the base galaxy, `ShatteredSpace.esm` and the
other DLC/Creations masters for their worlds. The mod’s **EsmReader** opens every
plugin in the load order once (v1.5.0+; earlier versions read only
`Starfield.esm`), decompresses each planet record (the records are
zlib-compressed; we bundle a tiny inflate library rather than poke the engine’s),
remaps each file’s local form IDs to their runtime values (later plugins override
earlier ones, the game’s own rule), and parses out the fauna and flora form IDs.
That gives a complete **species → planets** map with no visiting required —
**182 biome-bearing worlds, ~1,100 unique species** in the base game, plus
whatever your DLC and Creations add (Shattered Space brings it to 185).

---

## The flow: how a command completes worlds

*(This section describes the shipped v1.2.0+ design — everything is **ref-free
data writing**: no spawning, no teleporting. The earlier spawn-one-creature
approach is preserved as history in
[`green-outline-attempts.md`](green-outline-attempts.md).)*

The four commands share one core. `CompletePlanet` runs it on the world you're
standing on; `CompleteBarrenPlanets` / `CompleteLifePlanets` loop it across
their world lists; `CompleteAllPlanets` runs both sweeps with one combined
result. Each takes a category string (`resources,traits,fauna,flora` or `all`)
and touches **only** the requested categories.

### The barren sweep *(fast, native C++)*
A native sweep walks **all ~1,798 bodies** and, for every world with **no
authored flora/fauna**:
- pokes the engine's own “fully survey this planet” entry point so a knowledge
  entry is created and the **Survey Data slate** drops (`ID_102650`);
- writes the attribute “known” bits and resource scan flags **directly into the
  knowledge entry** (`ID_124898` / `ID_124899`) — the same bytes a real scan
  sets.

This is raw memory writing through the engine's own writers, so the whole sweep
happens in essentially one breath. A Papyrus **finalize pass** then walks the
swept list one world per iteration (the script VM naturally spreads it across
frames): it re-confirms each world reached a true 100% (the engine creates some
knowledge entries asynchronously, so a few stragglers need a second stamp),
fires the completion check (`ID_97853`), and marks each world's **traits** as
known (`ID_52155`).

> **Side effect — XP.** That per-planet “fully survey” call (`ID_102650`) is the
> same routine a normal orbital scan uses, so it grants survey XP. Across the
> galaxy that's a large level jump. The raw byte writes grant none; the XP comes
> from this one engine call.

Worlds **with** flora/fauna are deliberately excluded from this sweep — stamping
them “surveyed” without greening their species would claim 100% with unscanned
life, an invalid state the mod refuses to create.

### Life worlds — green as pure data *(the clever part)*
For every world with authored species (the base game has 182; DLC adds more —
185 with Shattered Space), completion is a per-world sequence of knowledge-DB
writes, valid for the world you're on **or** a never-visited one across the
galaxy:

1. **Ensure the knowledge entry exists** — the ref-free engine discover
   (`ID_102650`) creates it for a never-visited world (skipped for the world
   you're standing on, whose entry already exists).
2. **Resources**: mark the attribute bits and resource flags (as in the sweep).
3. **Traits**: mark each trait known (`ID_52155`). The in-world “0 of N SCANNED”
   landmark objects can't be driven remotely (they aren't loaded), but because
   the trait is now *known*, the game's own `CheckForScanTargetUpdate` resolves
   them the moment you arrive; on the world you're standing on, the mod drives
   the vanilla survey quest's own API (`SQ_Parent.DiscoverMatchingPlanetTraits`)
   to finish them immediately.
4. **Flora/fauna — the green**: for each authored species on that world, write
   the same per-(planet, species) knowledge-entry state a real hand scan writes:
   the **scan-flag byte** (what the outline colour reads), the **percent byte**
   (what the survey % math reads), and the **attribute-marker catalogue**
   (genetics / reproduction / temperament / abilities — the rows the scanner
   panel shows), with the marker set derived offline from the plugin files (see
   below). No creature is ever spawned; the target planet is addressed by form
   id.

Re-runs are true no-ops: any species whose scan flag and marker set are already
complete is skipped, so repeat commands don't inflate statistics or re-fire
completion events.

### Pacing

| Stage | Batching | Why it never hitches |
|---|---|---|
| Barren sweep | All bodies in one native call | Native writes are near-instant |
| Finalize / traits / life worlds | One world per script iteration | Papyrus yields across frames by design — a tight native loop isn't needed, and the script VM's own scheduling is the brake |

One-line model: **the native pass has no brakes; the script passes inherit the
VM's brakes.** There is nothing to throttle beyond that because nothing is
spawned — every step is a bounded data write.

---

## By the numbers (confirmed in testing)

- **1,798 / 1,798** planets and moons completed (survey data).
- **~1,100** unique flora/fauna species in the base game, across **182**
  biome-bearing worlds — **185** with Shattered Space, plus whatever other
  DLC/Creations add.
- Confirmed end-to-end: run the command → fly to a **never-visited** planet →
  its flora and fauna spawn **green**, with the full scanned attribute panel.
- Confirmed on DLC worlds (v1.5.0): Va'ruun'kai completes via the hand scanner,
  the orbital scanner, and the console commands.

---

## The auto-complete toggles (v1.3.0+ / v1.4.0+)

The two Settings → Gameplay toggles are crash-safe hooks on two *call sites* of
the engine's survey check-and-notify (`ID_97853`) — the single function every
survey change in the game funnels through:

- **Hand Scanner** — the on-surface hand-scan site (`ID_52157`): finish scanning
  anything on foot and the mod completes that world.
- **Orbital Scanner** — the star-map scan site (`ID_52173`), filtered to the
  *scan-level-changed* reason: scan a body on the galaxy map and the mod
  completes it, then re-invokes the engine's own panel-populate call so the
  info panel repaints to 100% in place.

Both hooks only *observe* the call and queue work for later dispatch (after the
scanner UI closes), so the engine's own scan flow is never interrupted.

---

## Under the hood: the native functions used

These are reverse-engineered engine functions, addressed version-independently via
the Address Library (`REL::ID(n)`). Names are the mod’s own.

| ID | Mod name | Role |
|---|---|---|
| `126578` | `GetKnowledgeManager` | Singleton holding the per-save knowledge database |
| `102650` | `ScanCompletePlanet` | Engine’s ref-free “fully survey a planet” (creates the entry, drops the slate, grants XP) |
| `124898` / `124899` | `IncrementScanFlag` / `SetPercentByte` | Per-species scan-flag and percentage writers in the knowledge entry (the outline “green”) |
| `52155` | `SetTraitKnownNative` | Marks a planet trait as discovered (and fires its event) |
| `97853` | `SurveyCheckNotify` | Survey check-and-notify — the convergence point; also where the two toggles hook (via its `ID_52157` / `ID_52173` call sites) |
| `126806` | `DbLookup` | The knowledge DB's own hash-map lookup (resolving a planet's entry without creating one) |
| `52188` | *(avoided)* | The “which planet am I on?” location resolver we deliberately bypass |

The species **marker catalogue** (the scanned panel's genetics / reproduction /
temperament / abilities rows) is not written by an engine call at all: the mod
derives each species' exact marker set offline by evaluating the game's own
authored `CTDA` conditions from the HandScanner catalog FormLists, and writes
the result into the knowledge entry — byte-identical to a real scan (validated
against in-game ground truth, 17/17).

Plus the **EsmReader** (project code, not an engine call): opens every plugin in
the load order (`Starfield.esm`, DLC, Creations), inflates each `PNDT` record,
remaps file-local form IDs to runtime values, and parses `PPBD` sub-records into
the species → planets map that makes the never-visited planets reachable.

---

*Companion docs: [`green-outline-attempts.md`](green-outline-attempts.md) is the
full attempt-by-attempt record of how the green recipe was found; the C4 model
([`model.c4`](model.c4) / [`views.c4`](views.c4)) is the structural architecture.*
