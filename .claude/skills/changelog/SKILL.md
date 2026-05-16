---
name: changelog
description: |
  Maintain CHANGELOG.md for this mod with honest accounting: diff git tags,
  filter tooling/docs noise, and separate genuine player/runtime behavior
  changes from refactors and recompiles.

  USE WHEN: user asks "what changed between vX and vY", to update/write the
  changelog, to cut or document a release, to draft Nexus release notes, or
  asks for an honest summary of a version bump.

  COVERS: tag-to-tag diffing, noise filtering, Keep a Changelog formatting,
  the project's behavior-vs-refactor convention, Nexus-facing notes.
---

# Changelog Maintenance

This mod's owner wants the **truth**, not marketing. A version bump that is
"recompile + refactor + two small fixes" must say exactly that. Never imply
new functionality that isn't there. See the project quality bar: no
half-working or oversold features.

## Standard

This project follows the dominant OSS pair, and you must keep it that way:

- **[Keep a Changelog](https://keepachangelog.com/) 1.1.0** for the file
  format.
- **[Semantic Versioning 2.0.0](https://semver.org/)** for version numbers.

Do not invent a bespoke scheme or switch standards. The MD024
"duplicate heading" lint warnings on repeated `### Changed` / `### Fixed`
sections are *inherent to Keep a Changelog* — do not restructure the file to
silence them.

## Versioning (SemVer for an SFSE mod)

The mod's only hard dependency is SFSE / the game build. Decide the bump by
the **highest** applicable rule:

- **MAJOR** — player can't just drop it in: breaks saves, changes/renumbers
  the ESM or Settings toggle (GPOG/GPOF FormID), needs manual migration.
- **MINOR** — new backward-compatible player-facing capability.
- **PATCH** — bug fixes, crash guards, and **compatibility recompiles**
  (rebuild against new SFSE / game build, behavior unchanged).

**SFSE dependency rule (critical):** SFSE is hard-locked to one exact game
build; a game patch forces a new SFSE and a DLL recompile. That recompile is
a **PATCH**, *never* MAJOR — the mod's own contract didn't change. But it is
a hard runtime gate SemVer can't express (a DLL built for build X won't load
on build Y), so:

- Every release section **must** state the exact game + SFSE build it was
  built/tested against (derive from the `extern/CommonLibSF` submodule SHA
  and version strings in that tag's `README.md`).
- A release that *changes* that target must carry a prominent blockquote
  warning telling players to match their game/SFSE and which prior version
  to stay on otherwise.
- Never reorder or renumber already-published tags; note that pre-policy
  tags predate the written policy instead.

## Format

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/) 1.1.0:

- Newest version first; `[Unreleased]` section at top.
- Version headers: `## [X.Y.Z] — YYYY-MM-DD` (the tag's commit date).
- Group changes under `Added` / `Changed` / `Fixed` / `Removed`.
- This repo adds an **`Internal (no behavior change)`** group for
  refactors, recompiles, and tooling/docs churn — keep using it.
- Each release line records the **game / SFSE versions built and tested**
  against (read from the `extern/CommonLibSF` submodule bump and any
  README/version notes in that tag's diff).
- Maintain the `[X.Y.Z]: .../compare/...` reference links at the bottom.

## Procedure

1. Identify range. Latest tag and its predecessor:
   - `git tag --sort=creatordate`
   - `git log --oneline <prev>..<new>`
2. Get the real diff, not the stat headline:
   - `git diff --stat <prev> <new>` to see scope, then
   - `git diff <prev> <new> -- <actual source files>` for substance.
3. **Classify every change** into one of:
   - **Behavior** — observable by the player or changes runtime semantics
     (Papyrus `.psc`, native logic affecting scan/survey, ESM, GMSTs).
   - **Fixed** — crash guards, correctness, bug repair.
   - **Internal** — refactors, magic-number→constant, helper extraction,
     `CommonLibSF` submodule bump / recompile, comment changes.
   - **Noise** — Claude skills, `.mcp.json`, `docs/*.c4`, CI/workflows,
     README. List under Internal as "Repo tooling/docs only: …", one line.
4. Be explicit when a release is *not* a feature release. State it in the
   first line of the version section.
5. A `CommonLibSF` submodule bump with unchanged REL::IDs is a
   **compatibility recompile**, not a feature — say so, and record the new
   game/SFSE target.

## Noise filter (this repo)

Treat as Internal/noise unless the user says otherwise:
`.claude/`, `.agents/`, `.mcp.json`, `skills-lock.json`, `docs/*.c4`,
`.github/workflows/*`, `README.md`, `.gitignore`.

Treat as substance:
`src/**`, `include/**`, `Data/Scripts/Source/**/*.psc`, `*.esm`/`*.esp`,
`extern/CommonLibSF` (submodule pointer), `xmake.lua`/build files.

## Cutting a release

1. Decide the version number from the **Versioning** rules above (highest
   applicable bump). Move `[Unreleased]` content into a new
   `## [X.Y.Z] — <date>` section; leave `[Unreleased]` with
   "*No unreleased changes.*".
2. Update the compare-link references at the bottom.
3. Sync any version-specific claims (the README intentionally carries **no**
   version pin — keep it that way; compatibility lives in CHANGELOG.md only).
4. Offer Nexus-facing notes: the same content, prose-trimmed, with the
   honest framing intact ("compatibility recompile for 1.16.x", etc.).

## Don'ts

- Don't bump MAJOR for a compatibility recompile (it's always PATCH).
- Don't restructure the file to silence MD024 — duplicate `###` section
  headings are required by Keep a Changelog.
- Don't restate the `git diff --stat` line count as if it were impact.
- Don't fold refactors into "Changed" to pad a release.
- Don't reintroduce a fixed game/SFSE version into README.md.
- Don't claim a feature works "across all biomes / 100%" unless that tag's
  diff actually delivers it end to end.
