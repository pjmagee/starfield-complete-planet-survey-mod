# Feature Plan: NG+ Lore Trigger + Per-System Selection UI

Status: **planned / not started.** Captured during the green-outline work; depends
on the galaxy data sweep (done) and the green pass (in progress) being stable
first. Two related features that share the same per-system plumbing.

---

## Part A — New Game Plus lore trigger ("you've charted these stars before")

### Concept

Starfield's NG+ premise is *déjà vu* — the character has lived this before. Tie
the bulk survey completion to that: on entering a new universe, the character is
hit with an uncanny familiarity, and the planetary data **resolves itself** as
buried knowledge surfaces. Then they shake it off — a headache, nothing a little
aspirin won't fix — and carry on. This gives the "everything is already surveyed"
state an in-fiction reason instead of a raw console command.

### Beat sheet (flavor)

1. Faint screen pulse / blur as the new universe loads (imagespace modifier).
2. Notification cascade: *"A strange familiarity… you've walked these worlds."*
3. The survey data processes (existing `CompleteAllPlanetsSurveyData` + green pass)
   — surfaced as the "<Planet> Survey Data … added" toasts already firing.
4. *"You shake your head. Just a headache. Some aspirin will sort it."*

### Implementation sketch

- **Trigger.** Detect the NG+ transition. Candidates to investigate:
  - The "Unity" questline completion / the universe-count global that increments
    each NG+ (RE or CK to find the exact form — Starfield tracks NG+ count for the
    "experienced traveller" dialogue gating).
  - A `Start Game Enabled` quest whose script checks the NG+ count on load and
    fires once per increment (persist "last seen count" in the quest).
- **Action.** Quest script calls the same entry point the console command uses
  (`CompleteAllPlanetsSurveyData`, later + `GreenAllPlanets` once stable), wrapped
  in the flavor messages.
- **Gating.** A GPOF toggle (`CPSCompleteOnNewGamePlus`, default **off** so it's
  opt-in — completing everything silently on NG+ is a strong default) under the
  existing `CPSGameplayOptions` GPOG. Reuses the [category-toggles](feature-category-toggles.md)
  GPOF pattern.
- **Cost estimate.** Moderate. The hard part is reliably detecting the NG+ moment
  (1–2 days of RE/CK). The quest + messages + imagespace are standard CK work
  (~1 day). No new native code if it reuses the existing completion entry point.

### Open questions

- Exact NG+ detection hook — is there a clean event/quest stage, or do we poll a
  global on load? Needs CK/RE spike.
- Should it complete **all** systems, or only systems the character has *visited*
  in this universe (more lore-consistent — you only "remember" where you've been)?
  The latter needs the per-system filter from Part B.

---

## Part B — Per-system selection ("pick which systems to complete")

### Concept

Instead of all-or-nothing, let the player choose **which star systems** get
completed, and re-open the picker any time as they play. Pairs naturally with
Part A ("remember" systems one at a time) and with players who want to preserve
the exploration loop for un-picked systems.

### What's needed under the hood (shared plumbing)

1. **Planet → system mapping.** Today the sweep enumerates *all* `PNDT` forms from
   the global form registry. Per-system needs each planet's parent system.
   - In data: `PNDT` records hang off a star/system (`STDT`) in the galaxy
     hierarchy. At runtime `BGSPlanet` carries its system reference.
   - Add `int Function CompleteSystemSurvey(Form akSystem)` (native) that runs the
     existing per-planet completion **filtered** to that system's planets, plus a
     per-system green pass (spawn only that system's species — far fewer than the
     ~1100 galaxy-wide set, so it sidesteps the mass-spawn crash entirely).
2. **System enumeration + names.** Enumerate `STDT` forms (the way we enumerate
   `PNDT`), expose `EnumerateSystems()` / `GetSystemAt(i)` / `GetSystemName(i)` so
   the UI can list them. Group by faction/region for a sane menu (UC, Freestar,
   Crimson Fleet space, etc.).

Per-system green is the big win: it makes the green pass **safe by construction**
(tens of spawns per system, not ~1100), which is the current galaxy-wide pain.

### UI options (the "how much work" question)

The Settings → Gameplay panel (GPOG/GPOF) is **toggles/sliders only** — it cannot
present a selectable *list* of 100+ systems. So the picker has to be one of:

| Option | What it is | UX | Effort |
|---|---|---|---|
| **A. `Message` menu** | Papyrus `Message` with up to ~10 buttons; paginate region → system | Clunky for 100+ entries; needs nested menus | **Low** (~1 day, pure Papyrus) |
| **B. Custom Terminal (`TERM`)** | In-world "Stellar Cartography Console"; menu tree per region→system, fragments call `CompleteSystemSurvey` | Best fit — looks/feels like a real panel; scales to all systems | **Moderate–High** (~2–4 days CK: terminal tree + fragments + how the player reaches it) |
| **C. Aid-item holotape** | Inventory item that opens a `Message` menu (FO4-MCM pattern) | Always accessible; same menu limits as A | **Low–Moderate** |
| **D. Starmap selection** | Read the starmap's currently-selected system, complete it via hotkey/console | Most natural ("select system, press button") | **Moderate–High** (needs the selected-system accessor — RE) |

**Recommendation:** **B (Terminal)** for the shipping UX, with **A (`Message`
menu)** as a fast first cut to validate the per-system completion before investing
in terminal authoring. The terminal can live on the player's ship or as a deployable
("survey console") aid item.

### Rough total effort

- Per-system native + system enumeration: ~2–3 days (some RE for planet→system).
- First-cut `Message` menu picker: ~1 day.
- Terminal UI: +2–4 days.
- NG+ trigger (Part A): ~2–3 days.

So a **first usable cut** (per-system completion + a Message-menu picker + the NG+
toggle) is roughly a week; the polished terminal panel + visited-only NG+ memory
is another week.

### Dependencies / sequencing

1. **Finish the galaxy green pass first** (current work) — per-system green reuses
   the exact capture+stamp code at a safer scale.
2. Planet→system mapping + `CompleteSystemSurvey` native.
3. `Message`-menu picker (validate).
4. Terminal UI + NG+ trigger.

### Out of scope (for now)

- Per-planet (vs per-system) selection — system granularity is enough.
- A full custom HUD/menu widget (Scaleform) — terminal is the ceiling.
- Cross-save/universe persistence beyond what GPOFs + the knowledge DB already do.
