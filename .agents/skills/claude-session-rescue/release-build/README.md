# Claude Session Rescue

A tool to fix broken Claude Code sessions caused by oversized images.

## The Problem

Claude Code sessions can break when base64-encoded images exceed the API's dimension limit (2000px). This often happens after sleep/wake cycles, when previously-working images fail on replay. The error message is misleading:

```
An image in the conversation exceeds the dimension limit for many-image requests (2000px).
Run /compact to remove old images from context, or start a new session.
```

Even `/compact` doesn't always work. The issue is tracked upstream at [anthropics/claude-code#13480](https://github.com/anthropics/claude-code/issues/13480).

## The Solution

This tool scans Claude Code session files (JSONL format) and removes base64-encoded images, replacing them with placeholder text. It creates numbered backups before modification, so you can always recover.

## Installation

### As a Claude Code Skill

```bash
# Ask Claude Code to install this skill from the repo
# Claude will handle copying to ~/.claude/skills/
```

### Direct Usage

```bash
# Run directly with uv
uv run --script scripts/session_rescue.py --session-name "your-session-name"

# Or copy the script to your PATH
cp scripts/session_rescue.py /usr/local/bin/
session-rescue --session-name "your-session-name"
```

## Usage

### Find a session by name

```bash
uv run --script scripts/session_rescue.py --session-name "google-messages-sms-feed"
```

### Find a session by ID

```bash
uv run --script scripts/session_rescue.py --session-id "03d04fb2-71eb-4929-9e65-2a698f2f337e"
```

### Dry run first (no modifications)

```bash
uv run --script scripts/session_rescue.py --session-id "xxx" --dry-run
```

### Process all sessions

```bash
uv run --script scripts/session_rescue.py --all
```

### Only process sessions with many images

```bash
uv run --script scripts/session_rescue.py --all --min-images 10
```

## How It Works

1. **Session Discovery**: Finds sessions in `~/.claude/projects/<project>/<session>.jsonl`
2. **Image Detection**: Recursively scans JSON for `type: "image"` with `source.type: "base64"`
3. **Backup**: Creates `.bak` (first) or `.bak.1`, `.bak.2`, etc. (subsequent)
4. **Processing**: Line-by-line JSONL parsing, image replacement with placeholder
5. **Atomic Write**: Temp file + rename for safety

## Safety Features

- **Backups first**: Never modifies without creating a backup
- **Dry-run mode**: See what would change without making changes
- **Active session check**: Skips sessions with running PIDs
- **Atomic writes**: Temp file + rename prevents corruption
- **Malformed JSON**: Passes through unchanged with warnings

## Output Example

```
Processed: /home/user/.claude/projects/my-project/session.jsonl
  Images removed: 24
  Characters removed: 3847291
  Backup: /home/user/.claude/projects/my-project/session.jsonl.bak

Total: 24 images removed (3847291 chars)
```

## Technical Details

### Session File Structure

Claude Code sessions are stored as JSONL (JSON Lines) files. Each line is an independent JSON object representing a message in the conversation.

Images appear in `tool_result` content arrays:

```json
{
  "type": "user",
  "message": {
    "role": "user",
    "content": [{
      "type": "tool_result",
      "tool_use_id": "toolu_xxx",
      "content": [
        {"type": "text", "text": "Screenshot captured"},
        {"type": "image", "source": {"type": "base64", "media_type": "image/jpeg", "data": "..."}}
      ]
    }]
  }
}
```

### Image Replacement

Images are replaced with a placeholder:

```json
{"type": "text", "text": "[image removed: image/jpeg, 123456 base64 chars]"}
```

This preserves the conversation structure while removing the large base64 data.

## License

MIT

## Contributing

Issues and PRs welcome. See the upstream issue for context: [anthropics/claude-code#13480](https://github.com/anthropics/claude-code/issues/13480)
