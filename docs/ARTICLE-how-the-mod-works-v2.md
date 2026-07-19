# Complete Planet Survey — How It Works, From the Author

*The mod description, as written by its (definitely real) author, Todd Howard.*

---

I've shipped a lot of games where you can see a mountain and climb it. This
time I made a mod for one of them. Starfield gives you 1,798 planets and moons,
and surveying all of them by hand is a beautiful, enormous amount of walking.
I love that for you. But I also built this, for the day you're done walking.

Complete Planet Survey finishes planet surveys — one world, the lifeless ones,
the living ones, or the whole galaxy — from a console command, or automatically
when you scan something, if you flip one of my two toggles. Here's what it
actually is, and every part of the game it touches. I kept it simple, because
the best systems are.

## What I ship you

Three small pieces, the classic shape of an SFSE mod:

- **A DLL** — the SFSE plugin. All the real work. It finds the engine's
  functions through the Address Library, so it survives game patches.
- **A tiny ESM** — it adds nothing to the world except two options records, so
  my "Hand Scanner" and "Orbital Scanner" toggles show up in the normal
  Settings → Gameplay menu and save like any other setting. No vanilla records
  are touched.
- **Papyrus scripts** — the console commands. `cgf` calls a script function,
  the script calls my native functions in the DLL. The console is the whole UI.

## The insight the mod is built on

In Starfield, a scanned creature's **green outline is not on the creature**.
Wildlife spawns fresh every time you land; each animal asks the save's
**knowledge database** — a per-save memory of everything you've surveyed —
"has this player scanned my kind, on this planet?" Green if yes, blue if no.

So surveying a planet remotely doesn't require going there, spawning anything,
or loading anything. It requires **writing the memory** a finished survey would
have left: for each species on that planet, a scan flag, a percent value, and
the list of scanner-panel entries. My mod writes exactly those, with the same
engine writers the real scanner uses, keyed the same way. Land there for the
first time next month and everything spawns green, because as far as your save
remembers, you did the work.

## How it knows what lives on a planet you've never visited

The running game can't tell me — it only builds a planet's species list when
you land. But the *files* know. Every planet's flora and fauna are authored in
the plugins: `Starfield.esm` for the base galaxy, `ShatteredSpace.esm` and
friends for theirs. So on startup my DLL reads the planet records straight out
of **your actual load order** — every master, base, DLC, Creations — doing the
FormID remapping across full, medium, and light plugins exactly by the game's
own rules, later files overriding earlier ones. That's how it knows Va'ruun'kai
has twenty species without ever having been to Va'ruun'kai.

The scanner panel's details — genetics, reproduction, temperament — aren't
stored per creature either. The game *computes* them at scan time from
condition lists authored in those same files. My mod runs the same conditions
against the same data and writes the same answer. A species I complete and a
species you scan by hand end up byte-identical in the save. I verified it. It
just works.

## Everywhere else, I press the game's own buttons

I didn't reimplement systems that already exist; I call them:

- **Lifeless worlds** — most of the galaxy — go through the engine's own
  "fully survey this planet" routine, the one an orbital scan uses. That's why
  the Survey Data slate lands in your inventory, and that's why you get survey
  XP for every world. Complete the galaxy and you will level up like it's a
  design flaw. It isn't. It's the vanilla reward code, doing its job.
- **Trait landmarks** — the "0 of 6 scanned" pillars — belong to the game's own
  survey quest, so I ask *that quest*, through its own functions, to finish
  them. Remote worlds resolve theirs the moment you arrive, because the trait
  is already known.
- **Your character stats** — Planets Scanned, Flora and Fauna Fully Scanned —
  are ordinary game statistics, incremented through the game's stat system.
- **The toggles** listen at the one engine function every scan in the game
  already reports to — the hand scanner from the ground, the star map from
  orbit. Scan something with a toggle on, and I finish that world and repaint
  the star-map panel to 100% using the game's own panel refresh.

And I'm careful with your game: everything is guarded and bounded, running a
command twice does nothing the second time, and worlds with life are never
stamped "surveyed" while their creatures are still blue — a half-done survey is
a bug, not a feature.

## The summary I'd put on the box

Your save keeps a diary of everything you've surveyed. This mod reads the
game's own files to learn what *should* be in that diary, then writes those
entries with the game's own pen. The galaxy simply remembers you've been
everywhere.

1,798 planets. Zero loading screens. It just works.
