---
name: release
description: |
  Cut and publish a release of this mod end-to-end: pre-release gates
  (merged PRs, CI, build, offline validation, the user's in-game checklist,
  SFSE-log health check), the three-place version bump, CHANGELOG via the
  changelog skill, tag -> CI (GitHub release + Nexus auto-upload), and the
  manual Nexus page steps.

  USE WHEN: user asks to cut/ship/tag/publish a release, "prep v1.6.0",
  "release this", "push to Nexus", or asks what's left before a release.

  COVERS: release gates, version-field sync (xmake.lua + Plugin.h + the
  CompatibleVersions duty), tag flow, Nexus group-id/paste steps, git
  signing gotcha, post-release verification.
---

# Cutting a release

The tag is the release source of truth (`vX.Y.Z` → CI builds, creates the
GitHub release, uploads to Nexus). Everything below exists to make sure the
tag lands on a commit that is actually releasable and that every version
string agrees with it.

## 1. Pre-release gates (all must pass)

1. **Everything merged**: `gh pr list` is empty (or only deliberately-held
   PRs); the release commit is on `main`; `git status` clean.
2. **CI green on main**: `gh run list --branch main --limit 3` — the head
   commit's Build and Package run passed.
3. **Local build**: `build.bat` → `[OK] Built ...CompletePlanetSurvey.dll`.
   (In a nested git worktree build.bat silently builds the WRONG dir — use
   `xmake f -m releasedbg -y -P . && xmake -y -P .` there. Root checkout is
   fine.)
4. **Offline validation**: if `src/EsmReader.cpp` or marker logic changed
   since the last tag, run `test\build_validate.bat` — it must pass its
   ground-truth counts (17/17 Jemison markers etc.) before any in-game test.
5. **In-game verification (USER-ONLY — never skip, never claim it
   yourself)**: `deploy.bat` (refuses while Starfield runs), then the user
   runs the checklists from the release's merged PR bodies plus any open
   `test/QA-*.md` protocols. Minimum healthy-log signature after their
   session (log: `…\My Games\Starfield\SFSE\Logs\CompletePlanetSurvey.log`):
   - `CheckOffsets: all N critical address-library ids resolve …`
   - `ResolveOffsets: bound N address-library relocations (direct RVA, no IDDB)`
   - `Bound Papyrus natives: …` (and again after a main-menu reload)
   - all three hook `installed at call-site` lines
   - **zero `[E]` / `[W]` lines**
   The log is TRUNCATED on every game launch — read/copy evidence for a run
   before the next launch overwrites it.

## 2. Version bump — three places, they DRIFT independently

Verified failure 2026-07-12: v1.5.0 shipped with `Plugin.h` still at
`{1,4,0,0}`, so the SFSE log banner said "CompletePlanetSurvey v1.4.0.0"
for a v1.5.0 install — misleading crash triage. The tag alone is what CI
releases, so nothing catches this unless you do:

1. `xmake.lua` → `set_version("X.Y.Z")`.
2. `include/Plugin.h` → `Plugin::Version{ REL::Version{ X, Y, Z, 0 } }`
   (this drives the log banner and SFSE plugin metadata).
3. `include/Plugin.h` → re-verify the `CompatibleVersions({...})` list is
   still true for this release (per-release duty documented at the list —
   extend/trim it when the verified game-build set changes).

Grep before tagging: `grep -rn "1\.<old minor>" xmake.lua include/Plugin.h`.

## 3. CHANGELOG + Nexus notes

Use the **changelog skill** (`/changelog`) — its "Cutting a release"
procedure: decide the SemVer bump, move `[Unreleased]` into the new
section, record the exact game + SFSE build tested against, update compare
links, draft the Nexus-facing notes. Honest accounting rules live there.

## 4. Tag → CI publishes

```
git tag vX.Y.Z && git push origin vX.Y.Z
gh run watch   # Build and Package: GitHub release + Nexus upload
```

- Commits/tags are SSH-signed via the 1Password agent: the 1Password
  desktop app must be running, and git write ops need the Bash sandbox
  disabled (`dangerouslyDisableSandbox: true`) or signing fails with
  `failed to fill whole buffer`.
- Nexus upload uses repo var `NEXUS_FILE_ID` = the mod-file **GROUP id**
  `7302980` (a per-upload file id 404s — validated since v1.2.0). Mod page
  is 16493.

## 5. Manual Nexus page steps (CI uploads only the file)

1. Paste `docs/NEXUS-DESCRIPTION.bbcode.txt` into the Nexus editor's
   **BBCode source view** (NOT the WYSIWYG view) on the Description tab.
2. Paste `docs/NEXUS-CHANGELOG.txt` into the Changelog tab.

## 6. Post-release

- Verify the GitHub release has the zip asset and the Nexus file page lists
  the new version.
- Confirm the freshly-deployed DLL's log banner shows the new version
  (catches a missed Plugin.h bump immediately).
- Update the memory project-status note (version, date, what shipped, what
  remains open).

## Don'ts

- Don't tag from a dirty tree or a branch other than `main`.
- Don't release with any merged-but-unverified behavior change — the
  in-game checklist is the user's gate, not yours.
- Don't hand-edit the GitHub release or re-tag a published version; fix
  forward with a PATCH.
- Don't put a version pin back into README.md (compatibility lives in
  CHANGELOG.md only).
