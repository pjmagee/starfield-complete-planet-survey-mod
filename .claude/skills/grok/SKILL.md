---
name: grok
description: Use the xAI **grok** CLI as a SEPARATE-USAGE subagent (its own grok.com billing, NOT Claude usage) to (a) run independent adversarial CODE REVIEWS of branches/diffs/PRs, and (b) offload background C++/Papyrus/RE/docs work in an isolated git worktree. Grok runs headless/non-interactively (`grok -p` / `--prompt-file`, JSON output) and already reads this repo's CLAUDE.md + skills. Use whenever you want a second opinion that doesn't spend Claude tokens, a review gate before committing risky native/offset/Papyrus changes, or extra parallel agents. Full CLI in references/cli.md.
---

# Grok (separate-usage review + background subagent)

`grok` (xAI "Grok Build", `~/.grok/bin/grok`, v0.2.6x) is an agentic coding CLI like Claude Code, but it
authenticates against **grok.com and bills on its OWN usage** — running it does **not** consume
Claude/Anthropic tokens. That makes it an ideal **independent reviewer** (a model that didn't write the
code, catching what we miss) and a **free-to-us extra worker** for parallel background tasks.

It discovers this repo's context: `grok inspect` shows it loads `CLAUDE.md`, the project **skills**, and
`.claude/settings.json`. So a Grok run here already knows the same rules we do.

**This is a Starfield mod repo** — an SFSE C++ plugin (CommonLibSF, `REL::Relocation`/`REL::ID` against the
game's address library; offsets = decompile addr − 0x140000000) calling decompiled engine functions across
the Papyrus↔native boundary, plus Papyrus scripts (`.psc`→`.pex`) and an ESM. Build = `build.bat`
(xmake/MSVC → the DLL) + the Papyrus compiler + `deploy.bat` (which **refuses while Starfield is running**).
**In-game behaviour can only be verified by the user** — neither grok nor Claude can run the game — so
background tasks are for code / RE / decompile analysis / save-parsing / docs, NEVER "test it in-game."

**Models:** `grok-composer-2.5-fast` (default) and `grok-build` (`grok models`). Login check: `grok models`
prints "You are logged in with grok.com." If it's not logged in, STOP and ask the user to run `grok login` —
never try to log it in or enter credentials.

## When to use Grok vs a Claude subagent

| Use Grok when… | Use a Claude `Agent`/worktree worker when… |
|---|---|
| You want an **independent review** (different model) of a branch/diff before committing risky native/offset/Papyrus changes | The work needs Claude-specific tools/MCP (Ghidra/Frida), the visualize MCPs, or tight coordination |
| You want to **save Claude usage** — reviews + scouting on grok's bill | The result must be committed onto the branch by the coordinator (grok works in a worktree) |
| You want **more parallel agents** for C++/Papyrus/RE/save-parsing work | You need to drive the running game / in-game verify (only the user can) |

Grok and Claude workers compose: spin up both; the Claude coordinator assembles + **builds** (`build.bat` +
Papyrus compile) + commits, and the **user does the in-game verify**.

## How to invoke (always non-interactive)

Call grok from the **Bash tool**. NEVER launch bare `grok` (it opens an interactive TUI that hangs a
non-interactive shell). Always one of:

- **Single-shot, prints to stdout & exits:** `grok -p "<prompt>"` (alias `--single`). Add
  `--output-format json` and parse `.text` (shape: `{text, stopReason, sessionId, requestId, thought}`).
- **Big prompt from a file** (preferred for diffs — avoids shell-escaping): `grok --prompt-file <path>`.
- Always wrap in a `timeout` (a large-diff review takes 1–5 min) and capture stderr separately (`2>err.txt`).
- Run it `run_in_background: true` for long jobs; you'll be notified on completion.

Key flags (full list: references/cli.md): `--output-format plain|json|streaming-json`, `--effort
low|medium|high|xhigh|max` (reasoning models only — the default `grok-composer-2.5-fast` **rejects** it),
`-m/--model`, `--rules "<extra system rules>"`, `--permission-mode default|acceptEdits|auto|dontAsk|bypassPermissions|plan`,
`--disallowed-tools <csv>` / `--tools <csv>`, `--disable-web-search`, `-w/--worktree [name]`, `--cwd`,
`-c/--continue`, `-r/--resume [id]`, `--best-of-n N` + `--check` (headless).

## Recipe A — Review a branch / diff / PR (READ-ONLY, the primary use)

The gate to run **before committing** risky native/offset/Papyrus changes (or right after a Claude
worktree worker reports, as a second opinion). It is read-only — a review run leaves the working tree CLEAN.

1. Get the diff. Uncommitted work: `git diff HEAD`. Local branch: `git diff main...<branch>`. A range:
   `git diff <baseSha>...HEAD`. PR (only if a GitHub remote exists): `gh pr diff <N>`.
2. Build a self-contained prompt = the standing criteria (`references/review-prompt.md`) + the diff,
   written to a temp file (so a huge diff doesn't hit shell-arg limits).
3. Run read-only and capture JSON:
   ```bash
   grok --prompt-file /tmp/grok-review-prompt.txt \
        --output-format json --disable-web-search --permission-mode plan \
        2>/tmp/grok-err.txt > /tmp/grok-review.json
   ```
   `--permission-mode plan` guarantees no edits/commits; `--disable-web-search` keeps it on the diff + repo.
   The helper does all of this: `bash .claude/skills/grok/scripts/grok-review.sh <working|branch|baseSha..|range|pr#>`.
4. Parse `.text`, then **triage like any reviewer** — act on real high/med findings, note false positives,
   don't blindly apply. Grok is adversarial and terse by design.

**The standing review criteria** (always sent — see review-prompt.md), tuned for this mod:
1. **Correctness** — C++/Papyrus bugs, edge cases, wrong `REL::ID`s / offsets / struct field offsets, misuse
   of CommonLibSF/SFSE/engine APIs, Papyrus type/cast errors, native↔Papyrus signature mismatches.
2. **Crash-safety & robustness** — null-check before every engine-pointer deref; `/EHa` try-catch around
   faultable engine calls; bounded loops + allocations; the live-component guard before fault-prone engine
   calls; the degraded-latch so one bad form doesn't kill the pass (a native fault crashes the player's game).
3. **Memory safety** — allocator-sensitive writes (this repo has a HISTORY of heap corruption from manual
   BSTArray/allocator pokes — prefer CommonLibSF containers); raw byte-pokes at hard-coded offsets; spawned-ref
   lifetimes; save/knowledge-DB writes that could persist an invalid state.
4. **Diagnostics** — spdlog `flush_on(info)` (Starfield-exit eats unflushed logs); per-phase logging; no
   silent catches.

## Recipe B — Offload background work (grok edits in a worktree)

For independent C++/Papyrus/RE/save-parsing/docs tasks you want done on grok's usage (extra parallelism):
```bash
# IMPORTANT (verified 2026-06-24): headless grok edits REQUIRE --always-approve. With acceptEdits /
# --permission-mode alone grok reports "applied" but NO edits persist (verified: the file was
# byte-unchanged). Add --disallowed-tools "Bash" to keep it read+edit only. Use --worktree for an
# isolated feature; OMIT it to edit the CURRENT working tree (e.g. apply review fixes to in-progress work).
grok --prompt-file /tmp/grok-task-prompt.txt --always-approve --disallowed-tools "Bash" --output-format json \
     --rules "Follow CLAUDE.md. Crash-safety: null-check before deref, /EHa-guard faultable engine calls, bound all loops/allocs. Prefer CommonLibSF containers over manual heap writes. spdlog flush_on(info). You CANNOT run the game — do not build or claim in-game results." \
     2>/tmp/grok-err.txt > /tmp/grok-task.json
```
- `--worktree` isolates the edits in a fresh git worktree. Afterwards the **Claude coordinator** assembles
  the files onto the branch, runs `build.bat` (+ the Papyrus compile), commits, and the **user does the
  in-game verify**. (`--effort` only with a reasoning model; omit it for the default model.)
- Good grok tasks here: decompile/RE analysis over the Ghidra dumps in `re/ghidra/output/`, save-format
  parsing/diff scripts (`re/save/*.py`), Papyrus refactors, docs — anything that does NOT need the running game.

## Long-running / socket modes (advanced — references/cli.md)
- `grok agent stdio` — JSON-RPC agent over stdio. `grok agent serve --bind 127.0.0.1:2419 --secret <token>`
  — WebSocket server. `grok agent leader` / `--leader-socket ~/.grok/leader.sock` — one warm agent, many
  sessions. `grok export <session>` → Markdown transcript; `grok sessions` to list.

## Hard rules
- **Read-only for reviews.** Always `--permission-mode plan` for review runs; confirm `git status` is clean
  afterward. Never let a review run edit/commit.
- **Never trust grok blindly.** Grok REVIEWS / drafts; the Claude coordinator validates (`build.bat` +
  Papyrus compile), and the **user** does the in-game verify. Treat grok findings as input, triage for false
  positives.
- **No in-game claims.** Grok can't run Starfield — it must never assert something "works in-game"; only the
  user confirms behaviour.
- **Don't enter credentials.** If `grok models` says not logged in, ask the user to `grok login`.
- **Separate billing.** Grok runs cost the user's grok.com usage, not Claude — prefer it for reviews +
  scouting, but it's still real spend; don't fan out dozens of runs frivolously.
- **Timeout + background.** Always `timeout`; use `run_in_background: true` for big reviews/tasks.

See references/cli.md (full CLI), references/review-prompt.md (criteria template), scripts/grok-review.sh
(the one-command review).
