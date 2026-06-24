# Grok CLI — full reference (v0.2.64)

`grok [OPTIONS] [PROMPT] [COMMAND]` — "Grok Build TUI". Binary: `~/.grok/bin/grok`. Default behaviour
with no `-p`/subcommand is an **interactive TUI** — never run that from a non-interactive shell.

## Top-level options

| Flag | Meaning |
|---|---|
| `[PROMPT]` | Initial prompt for an interactive session (e.g. `grok "fix the bug"`). For automation use `-p`. |
| `-p, --single <PROMPT>` | **Single-turn**: print the response to stdout and exit. The core non-interactive entrypoint. |
| `--prompt-file <PATH>` | Single-turn prompt read from a file (use for large diffs — avoids arg-length limits). |
| `--prompt-json <JSON>` | Single-turn prompt as JSON content blocks. |
| `--output-format <FMT>` | `plain` (default), `json`, `streaming-json`. JSON shape: `{text, stopReason, sessionId, requestId, thought}`. |
| `--agent <NAME>` | Agent name or definition file path. |
| `--agents <JSON>` | Inline subagent definitions as JSON. |
| `--no-subagents` | Disable subagent spawning. |
| `--allow <RULE>` | Permission allow rule (≈ Claude `--allowedTools`). |
| `--deny <RULE>` | Permission deny rule (≈ Claude `--disallowedTools`). |
| `--tools <CSV>` | Built-in tools to ALLOW (comma-separated). |
| `--disallowed-tools <CSV>` | Built-in tools to REMOVE (comma-separated). |
| `--permission-mode <MODE>` | `default`, `acceptEdits`, `auto`, `dontAsk`, `bypassPermissions`, `plan`. **Use `plan` for read-only review.** |
| `--always-approve` | Auto-approve all tool executions (dangerous; only for trusted background edits). |
| `--sandbox <PROFILE>` | Sandbox profile for filesystem + network access (`GROK_SANDBOX=`). |
| `--disable-web-search` | Disable web search + web fetch tools (keeps a review on the diff/repo). |
| `--effort <LEVEL>` | `low`, `medium`, `high`, `xhigh`, `max`. **Reasoning models only — the default `grok-composer-2.5-fast` rejects it.** |
| `--reasoning-effort <EFFORT>` | Reasoning effort for reasoning models. |
| `-m, --model <MODEL>` | Model id (`grok models` lists them: `grok-composer-2.5-fast` default, `grok-build`). |
| `--max-turns <N>` | Max agent turns. |
| `--best-of-n <N>` | Run the task N ways in parallel, pick the best (**headless only**). |
| `--check` | Append a self-verification loop to the prompt (**headless only**). |
| `--rules <RULES>` | Extra rules appended to the system prompt (inject review/quality criteria here). |
| `--system-prompt-override <PROMPT>` | Replace the agent system prompt (≈ Claude `--system-prompt`). |
| `--verbatim` | Send the prompt exactly as given. |
| `-w, --worktree [NAME]` | Start the session in a **new git worktree** (optionally named) — isolate edits. |
| `--cwd <CWD>` | Working directory. |
| `-c, --continue` | Continue the most recent session for the cwd. |
| `-r, --resume [SESSION_ID]` | Resume a session by id (or most recent). |
| `--restore-code` | Check out the original session's commit when resuming. |
| `--experimental-memory` / `--no-memory` | Cross-session memory on/off. |
| `--no-plan` | Disable plan mode. |
| `--no-alt-screen` | Run inline instead of the terminal alt-screen. |
| `--leader-socket <PATH>` | Use a custom leader socket (default `~/.grok/leader.sock`). |
| `--oauth` | Use OAuth at the welcome screen. |
| `--debug` / `--debug-file <FILE>` | Debug logging. |
| `-v, --version` / `-h, --help` | Version / help. |

## Subcommands

| Command | Purpose |
|---|---|
| `agent` | Run without the interactive UI. Sub: `stdio`, `headless`, `serve`, `leader`. |
| `models` | List models + show login state ("You are logged in with grok.com."). |
| `inspect` | Show the config Grok discovers here — CLAUDE.md, skills, permissions, login policy. |
| `sessions` | List / search / restore sessions. |
| `export` | Export a session transcript as Markdown. |
| `import` | Import sessions. |
| `mcp` | Manage MCP servers: `list`, `add`, `remove`, `doctor`. |
| `memory` | Manage cross-session memory. |
| `plugin` | Plugins + marketplace sources. |
| `worktree` | Manage git worktrees. |
| `leader` | Manage running leader processes. |
| `login` / `logout` | Auth (ask the USER to run `login`; never enter creds yourself). |
| `setup` | Fetch + install managed configuration. |
| `trace` | Export / upload session trace data. |
| `completions` | Shell completion scripts. |
| `update` | Check for updates / install a version. |
| `dashboard` | Open the Agent Dashboard view. |

### `grok agent <mode>` (headless server/embedding)

- `grok agent stdio` — agent over **stdio** (JSON-RPC; embed in a parent process). Opts: `--leader-socket`.
- `grok agent serve [--bind 127.0.0.1:2419] [--secret <TOKEN>] [--remote <URL>]` — **WebSocket server**.
  Secret auto-generated if unset (`GROK_AGENT_SECRET=`). Clients connect with the secret. `--remote` =
  proxy mode. Use for a persistent grok worker driven over WS.
- `grok agent headless [--grok-ws-url <URL>] [--grok-ws-origin <ORIGIN>]` — run over the Grok WS relay.
- `grok agent leader` — run as the shared **leader process** other grok clients attach to (one warm
  agent, many sessions; default socket `~/.grok/leader.sock`). `grok leader` manages running leaders.
- Common: `--model`, `--reasoning-effort`, `--always-approve`, `--reauth`.

## Useful invocation patterns

```bash
# Read-only review (JSON), no edits, no web:
grok --prompt-file prompt.txt --output-format json --permission-mode plan --disable-web-search

# Quick single answer, parse text (node is available on this machine):
grok -p "Explain the failure mode of X" --output-format json | node -e 'console.log(JSON.parse(require("fs").readFileSync(0,"utf8")).text||"")'

# Background build in an isolated worktree, repo rules + extra crash-safety criteria:
grok -p "<task>" --worktree grok-feat \
     --rules "Follow CLAUDE.md; null-check before deref; /EHa-guard faultable engine calls; bound loops/allocs; prefer CommonLibSF containers; you cannot run the game."

# Confirm login / models / discovered config:
grok models ; grok inspect
```

## Notes / gotchas

- **Separate billing**: grok auths against grok.com and bills xAI usage, NOT Claude/Anthropic.
- **It reads our context**: `grok inspect` confirms it loads `CLAUDE.md`, project skills, and
  `.claude/settings.json` permissions — so a run here already knows the repo rules.
- **It cannot run the game.** This is a Starfield mod — only the user can verify in-game behaviour. Never let
  grok claim something "works in-game"; it reviews/edits code and runs build/parse scripts only.
- A **review run is read-only** in practice (verified: working tree stayed clean) — still pass
  `--permission-mode plan` to guarantee it.
- Always wrap calls in `timeout`; capture stderr separately (`2>err.txt`); prefer `run_in_background`
  for big reviews/tasks.
- JSON output is a single object on stdout; debug/agent chatter goes to stderr.
- If a GitHub remote + `gh` exist, grok shares the authenticated `gh` and may post review comments
  (`gh pr comment <N>`) — COMMENT-ONLY; never `gh pr merge`/`gh pr close`/`git push`/force-push.
