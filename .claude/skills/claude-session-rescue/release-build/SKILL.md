---
name: claude-session-rescue
description: Fix broken Claude Code sessions caused by oversized base64 images
license: MIT
compatibility: claude-code>=2.1.0
metadata:
  category: Development
  tags:
    - claude-code
    - session-fix
    - image-cleanup
    - jsonl
  tested:
    - claude-code-2.1.100
    - python-3.9+
---

# Claude Session Rescue Skill

Fix broken Claude Code sessions caused by oversized base64 images.

## The Problem

Claude Code sessions can break when base64-encoded images exceed the API's dimension limit (2000px). This often happens after sleep/wake cycles. The error:

```
An image in the conversation exceeds the dimension limit for many-image requests (2000px).
```

Upstream issue: [anthropics/claude-code#13480](https://github.com/anthropics/claude-code/issues/13480)

## Installation

### Option 1: Clone and Install

```bash
# Clone the repo
git clone https://github.com/ianbmacdonald/claude-session-rescue.git
cd claude-session-rescue

# Install the skill (copies to ~/.claude/skills/)
# Or manually copy the scripts/ folder to ~/.claude/skills/claude-session-rescue/
```

### Option 2: Ask Claude Code

Just ask Claude Code to install this skill from the GitHub repo. It will handle the rest.

## Usage

### Find and Fix a Session by Name

```bash
session-rescue --session-name "your-session-name"
```

### Find and Fix a Session by ID

```bash
session-rescue --session-id "session-uuid-here"
```

### Dry Run First (Recommended)

```bash
session-rescue --session-id "xxx" --dry-run
```

### Process All Sessions

```bash
session-rescue --all
```

### Filter by Minimum Image Count

```bash
session-rescue --all --min-images 10
```

## Features

- **Session Discovery**: Find sessions by name, ID, latest, or all
- **Image Detection**: Recursively finds base64 images in JSONL
- **Numbered Backups**: `.bak`, `.bak.1`, `.bak.2`, etc.
- **Dry-Run Mode**: See what would change without modifying
- **Atomic Writes**: Temp file + rename for safety
- **Zero Dependencies**: Pure Python stdlib

## How It Works

1. Scans `~/.claude/projects/<project>/<session>.jsonl`
2. Detects `type: "image"` with `source.type: "base64"`
3. Creates backup before modification
4. Replaces images with placeholder text
5. Atomic rename to complete

## Safety

- Backups created before any modification
- Dry-run mode shows changes without applying
- Skips sessions with active PIDs
- Malformed JSON lines pass through unchanged

## License

MIT
