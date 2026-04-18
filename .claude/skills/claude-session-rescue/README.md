# Claude Session Rescue

A tool to fix broken Claude Code sessions caused by the image dimension limit bug (anthropics/claude-code#13480).

## The Problem

When you put your computer to sleep during a long-running Claude Code session with many screenshots, the session can break. Upon returning, every request fails with:

```
An image in the conversation exceeds the dimension limit for many-image requests (2000px).
```

The irony: the last task had nothing to do with images. The failure is retroactive—screenshots that worked fine during the session now trigger the API limit on replay.

## The Solution

This tool strips base64 images from your session JSONL files, freeing up context space and restoring functionality.

### Features

- **Safe**: Creates numbered backups before any modification (`.bak`, `.bak.1`, `.bak.2`, etc.)
- **Efficient**: Processes files line-by-line, handling 16MB+ sessions without memory issues
- **Complete**: Recursively finds images hidden in nested JSON structures
- **Atomic**: Uses temp file + rename to prevent corruption
- **Zero dependencies**: Pure Python stdlib—no pip, no venv, no setup

## Installation

### As a Claude Code Skill

```bash
skill install https://github.com/ianbmacdonald/claude-session-rescue
```

Then just ask Claude: "Fix my broken session with ID xyz123"

### As a Standalone Script

```bash
# Clone the repo
git clone https://github.com/ianbmacdonald/claude-session-rescue
cd claude-session-rescue

# Run directly with uv
uv run --script scripts/session_rescue.py --help
```

## Usage

### Find and Fix a Specific Session

```bash
# Dry run first—see what would be fixed
uv run --script scripts/session_rescue.py --session-id 03d04fb2 --dry-run

# Then fix it
uv run --script scripts/session_rescue.py --session-id 03d04fb2
```

### Fix the Most Recent Session

```bash
uv run --script scripts/session_rescue.py --latest
```

### Fix All Sessions

```bash
uv run --script scripts/session_rescue.py --all
```

### Options

| Flag | Description |
|------|-------------|
| `--session-id ID` | Find session by UUID prefix match |
| `--session-name NAME` | Find session by custom title |
| `--latest` | Process most recently modified session |
| `--all` | Process all sessions |
| `--project PATH` | Scope search to project directory |
| `--dry-run` | Report images found without modifying |
| `--min-images N` | Only process sessions with >= N images (default: 1) |
| `--verbose` | Detailed output |

## How It Works

1. **Session Discovery**: Finds JSONL files in `~/.claude/projects/<project>/`
2. **Backup First**: Creates `.bak` (or `.bak.1`, `.bak.2`, etc. for subsequent runs)
3. **Line-by-Line Processing**: Parses each line as JSON without loading the full file
4. **Recursive Image Detection**: Finds base64 images in nested structures like `tool_result.content[]`
5. **Image Stripping**: Replaces images with placeholders like `[image removed: image/jpeg, 197000 chars]`
6. **Atomic Write**: Writes to temp file, then renames to preserve permissions and prevent corruption

## Technical Details

### Session File Structure

Claude Code stores sessions as JSONL (JSON Lines) files:
- Each line is a独立的 JSON message
- Messages have types: `user`, `assistant`, `system`, `tool_result`, etc.
- Images are base64-encoded in `content` arrays
- Images nest deeply inside `tool_result` objects

### Image Detection Criteria

The tool looks for objects where:
- `type == "image"` AND
- `source.type == "base64"`

This avoids false positives from text that merely mentions "image".

### Backup Strategy

- First run: `session.jsonl.bak`
- Second run: `session.jsonl.bak.1`
- Third run: `session.jsonl.bak.2`
- And so on...

Always review your backups before deleting them.

## Contributing

Found a bug? Have a suggestion? Open an issue or submit a PR.

## License

MIT

## Acknowledgments

Built using [Lemonade](https://lemonade-server.ai) for local model inference.

See the [v10.2.0 release](https://github.com/lemonade-sdk/lemonade/releases/tag/v10.2.0) where we're named as a contributor.
