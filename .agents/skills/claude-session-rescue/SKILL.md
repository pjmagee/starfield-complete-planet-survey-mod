---
name: claude-session-rescue
description: Fix broken Claude Code sessions by stripping base64 images from session JSONL files.
---

# Claude Session Rescue

Fix broken Claude Code sessions caused by the image dimension limit bug (anthropics/claude-code#13480).

## What It Does

When a long-running Claude Code session breaks after sleep/wake with image limit errors, this tool:

1. Finds the broken session file
2. Creates a backup
3. Strips base64 images from the JSONL
4. Restores session functionality

## Install

```bash
skill install https://github.com/ianbmacdonald/claude-session-rescue
```

This installs the skill from the repo, which contains:
- `SKILL.md` - This file with instructions
- `scripts/session_rescue.py` - The execution engine

## Usage

After installation, just ask Claude:

> "Fix my broken session with ID 03d04fb2"

Or run the script directly:

```bash
# Dry run first
uv run --script scripts/session_rescue.py --session-id 03d04fb2 --dry-run

# Then fix it
uv run --script scripts/session_rescue.py --session-id 03d04fb2

# Fix most recent session
uv run --script scripts/session_rescue.py --latest

# Fix all sessions
uv run --script scripts/session_rescue.py --all
```

## Options

- `--session-id ID` — Find by UUID prefix
- `--session-name NAME` — Find by custom title
- `--latest` — Most recently modified session
- `--all` — All sessions
- `--project PATH` — Scope to project directory
- `--dry-run` — Report without modifying
- `--min-images N` — Threshold (default: 1)
- `--verbose` — Detailed output

## How It Works

1. **Backup first** — Creates `.bak`, `.bak.1`, `.bak.2` files
2. **Line-by-line processing** — Handles 16MB+ files without memory issues
3. **Recursive image detection** — Finds images in nested `tool_result` structures
4. **Atomic writes** — Temp file + rename prevents corruption

## The Problem

Sessions break when screenshots accumulate past the API's 2000px dimension limit. The failure is retroactive—images that worked during the session fail on replay after sleep/wake.

This tool removes the images while preserving the conversation text, freeing up context space.

## License

MIT
