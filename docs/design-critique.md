# Design Critique & Hardening Roadmap

*A candid review of the galaxy survey-completion command — what's solid, where
it's fragile, and how to decompose it into smaller commands. Findings were
produced by an adversarial multi-lens review and then **verified against the
source** (file:line cited); claims that can't be settled from code alone are
called out as **needs in-game test**.*

*Date: 2026-06-21 · Scope: `CompleteAllPlanetsSurveyData` and its three passes.*

---

## Verdict

The **algorithmic core is genuinely good** and the happy path — vanilla
`Starfield.esm`, standing on a surface, run once — is well-engineered. But the
command is **fragile the moment anything deviates**, and the three-pass design
relies on timing luck rather than synchronization for one correctness-critical
step. None of this is "broken"; it's a clear hardening + refactor backlog for a
shipped v1.1.0.

## What's solid (the adversarial reviewers agreed)

- **Pass 3's species inversion** — ~1,100 unique species, one live handle at a
  time, target planet passed explicitly — is the right design and avoids the
  obvious mass-spawn trap.
- **EsmReader bounds-checking** (`Cursor::can`, `kMaxListLen` caps on the PPBD
  sub-arrays) and the **memory-walk guards** (`capacity > 8M` rejection,
  `cell->IsAttached()`, the DB-miss sentinel at [Main.cpp:207](src/Main.cpp:207))
  are careful.
- **Offsets are centralized and documented** with Ghidra-derived comments and
  version ranges ([Main.cpp:7-146](src/Main.cpp:7)) — a single audit surface.
- **`IsLayoutDependent(true)`** is correctly set ([Plugin.h:21](include/Plugin.h:21)),
  so SFSE gates loading on the layout it was built for.

---

## Findings by severity (all code-confirmed unless noted)

### P0 — Safety: turns an edge case into a crash or silent corruption

1. **No exception/SEH handling anywhere across the native boundary.** There is no
   `try`/`catch`/`__try` in `src/` or `include/` — any throw or access-violation
   inside a Papyrus native propagates uncaught into the VM's C frame (UB / hard
   CTD, no log). The natives call straight into raw-pointer engine code (e.g.
   [Main.cpp:210](src/Main.cpp:210)). **This is the #1 robustness gap.**
   → *Fix: wrap every bound native body in a top-level `try/catch` + SEH guard
   that logs and returns a safe default.*

2. **Unbounded ESM allocation.** [EsmReader.cpp:174-177](src/EsmReader.cpp:174)
   sizes the decompression buffer from an **unvalidated** `u32 decompSize` read
   straight from the record (`decomp.assign(decompSize, 0)`); the only guard is
   `size < 4` on the *record*, not on `decompSize`. A corrupt/truncated/foreign
   `Starfield.esm` → multi-GiB alloc → `bad_alloc` on launch. Every *other* length
   in that file is capped by `kMaxListLen`; this one isn't.
   → *Fix: reject `decompSize` above a sane ceiling before allocating; degrade to
   an empty map + one warning rather than crash.*

3. **A missing Address-Library ID aborts the game.** An unresolved `REL::ID`
   routes through `REX::FAIL`, which pops a message box and calls
   `TerminateProcess` — a hard kill, no fallback. After a game patch SFSE ships
   before the address library (the project's own release-skew note), so a routine
   versionlib lag becomes **crash-on-launch**. There is no load-time offset
   self-check.
   → *Fix: a `CheckOffsets()` at `kPostDataLoad` that probes the critical IDs and,
   on failure, sets a `g_offsetsValid=false` flag the natives + poller check —
   degrading to "feature disabled" instead of aborting.*

### P1 — The three-pass design itself

4. **The cross-pass ordering is a race, not a guarantee.** Pass 1's knowledge-entry
   creates (`ID_102650`) are **asynchronous**; the Pass 2 finalize loop
   ([psc:77-88](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc:77)) is a
   plain `While` with **no flush barrier, no callback, no frame-floor** — it just
   hopes Papyrus scheduling outlasts the create latency. If an entry isn't ready,
   `ResolvePlanetSubobj` returns null and the writes **silently no-op**.

5. **Stragglers are left permanently partial, invisibly.** A planet whose entry
   misses the window keeps its dropped slate but never gets its attribute bits /
   scan flags, and there is **no post-loop retry** and **no per-planet failure
   report** — only an aggregate "X / count at 100%" log line
   ([psc:90](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc:90)).
   → *Fix: have Pass 1 return the not-ready set; re-queue those (bounded) and log
   exactly which planets never resolved.*

6. **No transaction boundary; irreversible step fired first.** Pass 1 fires
   `ID_102650` (discover + survey-complete event → the Survey Data slate, and per
   our in-game test, XP) at [Main.cpp:582](src/Main.cpp:582) **before** the
   survey-state writes (585/589). An abort mid-sweep leaves "slate dropped but data
   unwritten." → *Fix: write data first, fire the completion event last.*

7. **Off-surface = silent half-completion.** Run from a ship interior or orbit and
   Passes 1+2 finish (data, slates, traits) but `GreenAllPlanets` early-returns
   ([psc:106-109](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc:106)) with
   one toast — the galaxy reads "complete" but **all flora/fauna stay blue
   forever**. → *Fix: block the whole command up front with Pass 3's precondition,
   or persist a "green pending" flag and tell the player to re-run on a surface.*

### P2 — Performance

8. **Pass 1 is the real bottleneck.** It iterates every PNDT planet (~1,798) in a
   **single uninterrupted native loop with no yields**
   ([Main.cpp:551-602](src/Main.cpp:551)) — a guaranteed multi-hundred-ms-to-second
   hitch that scales with content. → *Fix: chunk across frames (resumable cursor
   driven by the Papyrus loop).*

9. **Redundant DB lookups.** The planet subobj is re-resolved via `DbLookup`
   (`ID_126806`) **per species**, inside `MarkSpeciesScannedForPlanet`
   ([Main.cpp:241](src/Main.cpp:241)) called per-species by both
   `MarkEsmSpeciesForPlanet` and `MarkEverythingForPlanet` — never cached once per
   planet. → *Fix: resolve the subobj once per planet and pass it down.*

10. **Pass 2 re-does all the expensive work.** `FinalizeSweptPlanet` re-runs the
    full `CompletePlanetSurveyState` + `MarkEsmSpeciesForPlanet` for **every** swept
    planet ([Main.cpp:878-881](src/Main.cpp:878)), not just the async stragglers —
    on the slow Papyrus path, relying on idempotency. → *Fix: finalize only the
    not-ready set; fire the per-planet completion event separately.*

11. **Pass 3's throttle is an unmeasured constant.** `Utility.Wait(0.05)` every 16
    species ([psc:126-129](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc:126))
    — ~3.4s of blind waiting across ~1,100 species, with no frame-time or
    queue-depth budget. **None of the three pacing constants in this system appear
    to have been profiled.** → *Fix: self-tuning time-budget gate
    (`GetCurrentRealTime`, yield at ~8-10ms/frame).*

### P3 — Maintainability / version fragility

12. **Version contract is narrower than the verified range.** The manifest declares
    `CompatibleVersions({ RUNTIME_LATEST })` = **1.16.244 only**
    ([Plugin.h:22](include/Plugin.h:22)), while the source comments + changelog
    document the same IDs/offsets valid across **1.16.236–1.16.244**. With
    `IsLayoutDependent(true)` the plugin refuses to load on 236/242 despite being
    compatible. → *Fix: either expand the list to match the verified range, or
    correct the "236–244" comments to "244 only" — make them provably agree.*

13. **Hardcoded ESM FormIDs, asymmetric failure.** `0x807` (popup) and `0x80C`
    (toggle) are pinned in Papyrus. A missing `0x807` is **silently skipped** (no
    log); a missing `0x80C` **logs and returns early, disabling auto-complete**
    (fail-*closed*) ([psc:59,147](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc:59)).
    The CK reassigns FormIDs on save and can't edit the master in place, so a
    regen can shift these. → *Fix: centralize the IDs + a single resolve-or-log
    helper so `0x807` also logs when missing.*

14. **A second, undocumented version coupling.** `FindCallSite`
    ([Main.cpp:1066-1084](src/Main.cpp:1066)) sig-scans ~0x400 bytes of `ID_52157`
    for an `E8` CALL whose target is **`ID_97853`** and trampoline-patches it. A
    compiler reorder on a rebuilt game moves the call → auto-complete-on-scan
    silently no-ops (error log only, no in-game signal).

15. **No tests, no offset self-check.** No `test/` dir; nothing validates a
    resolved pointer before writing into engine structs. → *Fix: a host unit test
    for `EsmReader` (hand-built PNDT/PPBD fixture + a malformed one) catches PPBD
    drift and the OOM path in CI, which today only compiles.*

16. **Cross-frame global caches are re-entrancy-unsafe.** `g_sweepPlanetForms`,
    `g_planetSpeciesCache`, `g_allSpeciesCache` are consumed by index from Papyrus
    across many frames; a second console invocation mid-loop refills the cache
    under the live index. → *Fix: an "in-progress" atomic gate.*

### Behavioral unknowns — **needs in-game test** (do not assume)

- **XP re-grant idempotency.** Re-running re-fires `ID_102650` for all ~1,798
  planets with no mod-side "already discovered" guard. Whether a second run
  re-grants the full survey-XP jump depends on engine de-dup the code only
  *assumes*. **Test: note level, re-run on the same save, check.**
- **Slate re-fire idempotency.** Pass 1 (`ID_102650`) and Pass 2 (`ID_97853`) both
  fire a completion event per planet, every run. If the engine doesn't de-dup, a
  re-run floods inventory with ~1,798 duplicate Survey Data slates. **Test the
  same way.**
- **GMST save-persistence.** `ApplyInstantScanGameSettings`
  ([Main.cpp:684-686](src/Main.cpp:684)) sets `iHandScanner*CountBase = 1` every
  `kPostDataLoad`. If `SetSetting` bakes into the `.sav`, uninstalling leaves the
  game altered. **Test: set, save, uninstall, reload — is it still 1?** (This is
  an existing open item.)

---

## Decomposition: "traits-only / data-only / per-system" — yes, here's the shape

The passes already separate cleanly along **two orthogonal axes**. Don't hand-write
a dozen commands — build the grid from small reusable cores.

### Axis 1 — Category (which native subset)

| Core | Status | Needs player on a surface? |
|---|---|---|
| **DataCore** = `CompletePlanetSurveyState` ([Main.cpp:456](src/Main.cpp:456)) | already exists; already shared by `MarkResourcesForPlanet` + `FinalizeSweptPlanet` | **No** — ref-free |
| **TraitsCore** = `MarkTraits` ([psc:228](Data/Scripts/Source/User/CompletePlanetSurveyQuest.psc:228)) | already exists | **No** — ref-free |
| **GreenCore** = new `GreenSpeciesOnPlanet` (factor out of `GreenSpeciesEverywhere`) | **new** — exactly what [refactor-unify-completesurvey-explicit-planet.md](refactor-unify-completesurvey-explicit-planet.md) proposes | **Yes** — live `PlaceAtMe` handle + the data entry must pre-exist |

### Axis 2 — Scope (which planet set)

- **Galaxy** = the existing `ForEachFormOfType(kPNDT)` walk
  ([Main.cpp:567](src/Main.cpp:567)), promoted to a reusable `EnumerateAllPlanets`
  native (Count/At).
- **System** = a **Papyrus** filter on the planet's parent star — there is **no
  C++ system enumerator**, only the galaxy walk.
- **Planet** = current planet.

Every command becomes **`enumerator × cores`** → `Complete<Scope>Survey<Category>`
(e.g. `CompleteAllSurveyTraits`, `CompleteSystemSurveyData`). The two headlines stay
thin wrappers; nothing re-implements a pass.

### Two couplings to respect

- **Traits-only is not independently runnable today** — the trait loop drives off
  `g_sweepPlanetForms`, populated *only* by the data sweep
  ([Main.cpp:553,590](src/Main.cpp:553)). The one genuinely new piece it needs is
  the `EnumerateAllPlanets` native so traits/green stop depending on the data pass.
- **Green-without-data no-ops** — `ID_52161`/`ID_52158` require the planet's
  knowledge entry to already exist (enforced inside the engine functions; the C++
  wrappers only null-check inputs — [Main.cpp:282-307](src/Main.cpp:282)). Green
  commands must run DataCore first or refuse.

### Sequencing

Land the **refactor-unify** change first (it yields the clean planet-scope green
core and drops the spawn-and-scan *location* dependency). After that, the
[category toggles](feature-category-toggles.md) and these console commands all share
the same three cores — decomposition falls out of the refactor nearly for free.

---

## Prioritized backlog

1. **P0 safety trio** (small, highest risk reduction): exception/SEH guards on every
   native · cap `decompSize` before alloc · load-time offset self-check that
   disables gracefully instead of `REX::FAIL`.
2. **P1 pass correctness**: off-surface partial-state handling · straggler retry +
   per-planet failure logging · run the three behavioral in-game tests (XP, slate,
   GMST).
3. **P2 performance**: chunk Pass 1 across frames · resolve subobj once/planet ·
   Pass 2 straggler-only · self-tuning Pass 3 throttle.
4. **P3 cleanup**: reconcile the version contract · centralize FormIDs · re-entrancy
   gate · `EsmReader` unit test in CI.
5. **Refactor + decompose**: land refactor-unify, then build the `category × scope`
   command grid from the three cores.

---

*Companion docs: [how-it-works.md](how-it-works.md) (what the command does),
[green-outline-attempts.md](green-outline-attempts.md) (how the green recipe was
found), [refactor-unify-completesurvey-explicit-planet.md](refactor-unify-completesurvey-explicit-planet.md)
and [feature-category-toggles.md](feature-category-toggles.md) (planned refactors).*
