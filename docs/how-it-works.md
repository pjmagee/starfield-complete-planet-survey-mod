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
flora/fauna roster is *authored* in `Starfield.esm` (the `PNDT` records, `PPBD`
sub-records). The mod’s **EsmReader** opens that file once, decompresses each
planet record (the records are zlib-compressed; we bundle a tiny inflate library
rather than poke the engine’s), and parses out the fauna and flora form IDs. That
gives a complete **species → planets** map with no visiting required —
**182 biome-bearing worlds, ~1,100 unique species** in total.

---

## The flow: three passes

Running `cgf "CompletePlanetSurveyQuest.CompleteAllPlanetsSurveyData"` kicks off a
short narrative popup, then three passes:

### Pass 0 — the popup
A modal “recall” message (a custom `MESG` record, `CPSRecallMessage`) appears and
blocks until you press OK. It’s pure framing — it lands the story before the
cascade of survey-complete notifications begins.

### Pass 1 — stamp every planet *(fast, native C++)*
A native sweep walks **all ~1,798 planets** and, for each one:
- pokes the engine’s own “fully survey this planet” entry point so a knowledge
  entry is created and the **Survey Data slate** drops (`ID_102650`);
- writes the attribute “known” bits and every per-species scan flag / percentage
  byte **directly into the planet’s data record** (`ID_124898` / `ID_124899`).

Most of this is raw memory writing, which is why it’s fast — the whole pass
happens in essentially one breath, **with no pauses**.

> **Side effect — XP.** That per-planet “fully survey” call (`ID_102650`) is the
> same routine a normal orbital scan uses, so it grants survey XP. Across ~1,798
> planets that’s a large level jump. The raw memory writes grant none; the XP
> comes from this one engine call. (See the mod page for how to suppress it.)

### Pass 2 — finalize & mark traits *(careful, Papyrus script)*
A script then walks the list of swept planets **one at a time**, re-confirms each
is complete and fires its completion check (`ID_97853`), and marks each planet’s
**traits** as known (`ID_52155`). Papyrus is slow, so the game spreads this loop
**across many frames** so it never freezes — no explicit timers, just natural
yielding.

### Pass 3 — paint flora & fauna green *(the clever pass)*
This is where the “green = data” insight pays off. Instead of visiting planets, we
work **per species**:

1. Spawn **one invisible, throwaway instance** of the species right where you’re
   standing — on your *current* loaded planet (the only place anything can spawn).
2. Use that instance only as a **type sample**, and for every planet that hosts
   the species, write its “scanned” fact: the **per-type tree write** (`ID_52161`)
   plus the **count completion** (`ID_52158`), with the **planet named
   explicitly** each time.
3. Disable and delete the instance. Move to the next species.

Only **one creature is ever alive at a time** — spawn, write everywhere, delete.

> **Why spawn anything at all, if it’s just a data write?** Because the tree write
> resolves the species’ *canonical type key* from a **live reference’s** type
> info — the static base form returns a null key (an earlier attempt confirmed
> base forms yield `{0,0,0}`). So we need one real, spawned creature to read the
> key from. Its *location is irrelevant*; only its *type* is used, and the target
> planet is passed by name. The tree write **alone** only produces blue; the tree
> write **plus** the count completion is what produces green.

Every 16 species, the loop takes a **0.05-second breather** so the engine can
flush the spawn/delete churn before more are created.

---

## Why the pacing differs between passes

| Pass | Batching | Pauses? | Why |
|---|---|---|---|
| 1 — sweep | All ~1,798 at once | None | Native code is fast enough to do it in one shot |
| 2 — finalize/traits | One planet at a time | None explicit; the script VM spreads it across frames | A tight script loop would freeze the game or overrun its per-frame budget |
| 3 — green | One *species* at a time (~1,100), each fanning out to all its host planets | Yes — 0.05s every 16 species | It’s the only pass that spawns real objects; the breather lets spawn/delete settle so refs don’t pile up |

One-line model: **the native pass has no brakes; the script passes tap the brakes
so the engine keeps up.** The trick that keeps Pass 3 quick is working by *unique
species* (~1,100) rather than *planet × species* — and naming the target planet
directly so the engine doesn’t need you to be there.

---

## By the numbers (confirmed in testing)

- **1,798 / 1,798** planets completed (survey data).
- **~1,100** unique flora/fauna species, across **182** biome-bearing worlds.
- **~1,700** planet-species green records written.
- Confirmed end-to-end: run on Jemison → fly to a **never-visited** planet → its
  flora and fauna spawn **green**.

---

## Under the hood: the native functions used

These are reverse-engineered engine functions, addressed version-independently via
the Address Library (`REL::ID(n)`). Names are the mod’s own.

| ID | Mod name | Role |
|---|---|---|
| `126578` | `GetKnowledgeManager` | Singleton holding the per-save knowledge database |
| `102650` | `ScanCompletePlanet` | Engine’s ref-free “fully survey a planet” (creates the entry, drops the slate, grants XP) |
| `124898` / `124899` | `IncrementScanFlag` / `SetPercentByte` | Raw per-species scan-flag and percentage writers in the planet data record |
| `52155` | `SetTraitKnownNative` | Marks a planet trait as discovered (and fires its event) |
| `97853` | `SurveyCheckNotify` | Survey check-and-notify; the completion check that drops the Survey Data slate |
| `52161` | `TypeScanInner` | Per-type “scanned species” **tree** write (the green seed — half of it) |
| `52158` | `PlanetProgressInner` | Per-species **count completion** (the other half of the green) |
| `52188` | *(avoided)* | The “which planet am I on?” location resolver we deliberately bypass |

Plus the **EsmReader** (project code, not an engine call): opens `Starfield.esm`,
inflates each `PNDT` record, and parses `PPBD` sub-records into the
species → planets map that makes the never-visited planets reachable.

---

*Companion docs: [`green-outline-attempts.md`](green-outline-attempts.md) is the
full attempt-by-attempt record of how the green recipe was found; the C4 model
([`model.c4`](model.c4) / [`views.c4`](views.c4)) is the structural architecture.*
