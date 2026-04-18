#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# ///
"""
Session Rescue CLI - Strips base64 images from Claude Code session JSONL files.

Usage:
    uv run --script session_rescue.py --session-name NAME [--dry-run]
    uv run --script session_rescue.py --latest [--dry-run]
    uv run --script session_rescue.py --all [--dry-run]

This tool finds and removes base64-encoded images from Claude Code session files,
creating backups before modification. Useful for recovering from the broken session
bug that corrupts sessions with large base64 image data.
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path
from typing import Any


def find_sessions(session_id: str | None, session_name: str | None, latest: bool,
                  all_sessions: bool, project: str | None) -> list[Path]:
    """Find session JSONL files based on search criteria."""
    claude_dir = Path.home() / ".claude" / "projects"

    if not claude_dir.exists():
        return []

    candidates = []
    for project_dir in claude_dir.iterdir():
        if project_dir.is_dir():
            for jsonl_file in project_dir.glob("*.jsonl"):
                candidates.append(jsonl_file)

    if not candidates:
        return []

    # Filter by project if specified
    if project:
        project_path = Path(project).resolve()
        candidates = [c for c in candidates if c.resolve().is_relative_to(project_path)]

    # Filter by session name or ID
    if session_name or session_id:
        filtered = []
        for jsonl_file in candidates:
            # Check the session metadata file
            session_dir = jsonl_file.parent
            metadata_file = session_dir / "metadata.json"

            if metadata_file.exists():
                try:
                    with open(metadata_file, "r", encoding="utf-8") as f:
                        metadata = json.load(f)

                    name = metadata.get("name", "")
                    sid = metadata.get("id", "")

                    if session_name and session_name.lower() in name.lower():
                        filtered.append(jsonl_file)
                    elif session_id and sid.startswith(session_id):
                        filtered.append(jsonl_file)
                except (json.JSONDecodeError, IOError):
                    pass

        # Always update candidates, even if filtered is empty
        candidates = filtered

    # Get latest if requested
    if latest and candidates:
        candidates = [max(candidates, key=lambda p: p.stat().st_mtime)]

    return candidates


def check_session_active(session_path: Path) -> bool:
    """Check if a session has an active PID lock file."""
    session_dir = session_path.parent
    pid_file = session_dir / ".pid"

    if pid_file.exists():
        try:
            with open(pid_file, "r") as f:
                pid = f.read().strip()
            if pid:
                # Check if process exists
                os.kill(int(pid), 0)
                return True
        except (ValueError, OSError):
            pass

    return False


def strip_images_recursive(obj: Any) -> tuple[int, int]:
    """
    Recursively walk JSON structure and remove base64 images.

    Returns: (image_count, total_chars_removed)
    """
    image_count = 0
    chars_removed = 0

    if isinstance(obj, dict):
        # Check if this is an image block
        if obj.get("type") == "image" and isinstance(obj.get("source"), dict):
            source = obj["source"]
            if source.get("type") == "base64":
                data = source.get("data", "")
                media_type = source.get("media_type", "image/png")
                chars_removed = len(data)
                image_count = 1
                # Replace with placeholder - preserve other keys if any
                obj["type"] = "text"
                obj["text"] = f"[image removed: {media_type}, {chars_removed} base64 chars]"
                # Remove source key since it's no longer valid
                if "source" in obj:
                    del obj["source"]
                # Don't return early - continue to check for more images

        # Recurse into all values
        for key, value in list(obj.items()):
            count, chars = strip_images_recursive(value)
            image_count += count
            chars_removed += chars

    elif isinstance(obj, list):
        # Recurse into all items
        for item in obj:
            count, chars = strip_images_recursive(item)
            image_count += count
            chars_removed += chars

    return image_count, chars_removed


def process_jsonl_line(line: str) -> tuple[str, int, int, str | None]:
    """
    Process a single JSONL line, stripping images if present.

    Returns: (processed_line, image_count, chars_removed, warning)
    """
    line = line.rstrip("\n\r")

    if not line.strip():
        return line, 0, 0, None

    try:
        obj = json.loads(line)
    except json.JSONDecodeError as e:
        # Malformed JSON - pass through with warning
        return line, 0, 0, f"Malformed JSON line: {e}"

    image_count, chars_removed = strip_images_recursive(obj)

    # Preserve key order where possible (Python 3.7+ dicts maintain insertion order)
    processed_line = json.dumps(obj, ensure_ascii=False)

    return processed_line, image_count, chars_removed, None


def create_backup(session_path: Path) -> Path:
    """
    Create a numbered backup of the session file.

    First backup: {original}.bak
    Subsequent: {original}.bak.1, .bak.2, etc.
    """
    base = session_path
    bak_path = Path(str(base) + ".bak")

    # If no .bak exists, create it
    if not bak_path.exists():
        return bak_path

    # Find the next numbered backup
    counter = 1
    while True:
        numbered_bak = Path(str(base) + f".bak.{counter}")
        if not numbered_bak.exists():
            return numbered_bak
        counter += 1


def process_session(session_path: Path, dry_run: bool, min_images: int, verbose: bool) -> dict:
    """
    Process a session JSONL file, stripping base64 images.

    Returns: dict with processing results
    """
    result = {
        "path": str(session_path),
        "images_found": 0,
        "images_removed": 0,
        "chars_removed": 0,
        "backup_path": None,
        "warnings": [],
        "dry_run": dry_run,
    }

    # Check if session is active
    if not dry_run and check_session_active(session_path):
        result["error"] = "Session has active PID lock - skipping to avoid data loss"
        return result

    # First pass: count images without modifying
    total_images = 0
    total_chars = 0
    temp_path = Path(str(session_path) + ".tmp")

    try:
        with open(session_path, "r", encoding="utf-8") as infile, \
             open(temp_path, "w", encoding="utf-8") as outfile:
            for line_num, line in enumerate(infile, 1):
                processed, img_count, char_count, warning = process_jsonl_line(line)
                outfile.write(processed + "\n")
                total_images += img_count
                total_chars += char_count

                if warning:
                    result["warnings"].append(f"Line {line_num}: {warning}")
    except IOError as e:
        # Cleanup temp file on failure
        if temp_path.exists():
            temp_path.unlink()
        result["error"] = f"Failed to process session file: {e}"
        return result

    result["images_found"] = total_images
    result["chars_removed"] = total_chars

    # Check min_images threshold
    if total_images < min_images:
        # Remove temp file since we're not processing
        temp_path.unlink()
        result["skipped"] = f"Only {total_images} images found (min_images={min_images})"
        return result

    # Dry run - just report, remove temp file
    if dry_run:
        temp_path.unlink()
        return result

    # Create backup before modification
    try:
        backup_path = create_backup(session_path)
        shutil.copy2(session_path, backup_path)
        result["backup_path"] = str(backup_path)

        # Verify backup
        if not backup_path.exists() or backup_path.stat().st_size == 0:
            temp_path.unlink()
            result["error"] = "Backup creation failed"
            return result
    except IOError as e:
        temp_path.unlink()
        result["error"] = f"Failed to create backup: {e}"
        return result

    # Preserve original permissions on temp file
    try:
        shutil.copymode(session_path, temp_path)
        # Atomic rename (os.replace is more robust across platforms)
        os.replace(temp_path, session_path)
        result["images_removed"] = total_images
    except IOError as e:
        # Cleanup temp file on failure
        if temp_path.exists():
            temp_path.unlink()
        result["error"] = f"Failed to write session file: {e}"
        return result

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Strip base64 images from Claude Code session JSONL files",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    # Session selection (mutually exclusive)
    session_group = parser.add_mutually_exclusive_group()
    session_group.add_argument(
        "--session-id",
        metavar="ID",
        help="Find session by UUID prefix match",
    )
    session_group.add_argument(
        "--session-name",
        metavar="NAME",
        help="Find session by matching against metadata name field",
    )
    session_group.add_argument(
        "--latest",
        action="store_true",
        help="Process the most recently modified session",
    )
    session_group.add_argument(
        "--all",
        action="store_true",
        dest="all_sessions",
        help="Process all sessions in ~/.claude/projects/",
    )

    # Options
    parser.add_argument(
        "--project",
        metavar="PATH",
        help="Scope search to specific project directory",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report images found without modifying files",
    )
    parser.add_argument(
        "--min-images",
        type=int,
        default=1,
        metavar="N",
        help="Only process sessions with >= N images (default: 1)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed output including warnings",
    )

    args = parser.parse_args()

    # Validate that exactly one session selector is provided
    if not any([args.session_id, args.session_name, args.latest, args.all_sessions]):
        parser.error("One of --session-id, --session-name, --latest, or --all is required")

    # Find sessions
    sessions = find_sessions(
        session_id=args.session_id,
        session_name=args.session_name,
        latest=args.latest,
        all_sessions=args.all_sessions,
        project=args.project,
    )

    if not sessions:
        print("No sessions found matching criteria", file=sys.stderr)
        sys.exit(2)

    # Process each session
    total_images = 0
    total_chars = 0
    errors = []
    any_processed = False

    for session_path in sessions:
        result = process_session(
            session_path,
            dry_run=args.dry_run,
            min_images=args.min_images,
            verbose=args.verbose,
        )

        if "error" in result:
            errors.append(f"{result['path']}: {result['error']}")
            continue

        if "skipped" in result:
            print(f"Skipping {result['path']}: {result['skipped']}")
            continue

        any_processed = True

        # Report results
        if args.dry_run:
            action_word = "would remove" if result["images_found"] > 0 else "found"
            print(f"Dry run: {result['path']}")
            print(f"  Images {action_word}: {result['images_found']}")
            print(f"  Total characters: {result['chars_removed']}")
        else:
            print(f"Processed: {result['path']}")
            print(f"  Images removed: {result['images_removed']}")
            print(f"  Characters removed: {result['chars_removed']}")
            if result["backup_path"]:
                print(f"  Backup: {result['backup_path']}")

        if args.verbose and result["warnings"]:
            for warning in result["warnings"]:
                print(f"  Warning: {warning}")

        total_images += result["images_found"] if args.dry_run else result["images_removed"]
        total_chars += result["chars_removed"]

    # Summary
    if args.dry_run:
        print(f"\nDry run: Total: {total_images} images found ({total_chars} chars)")
    else:
        print(f"\nTotal: {total_images} images removed ({total_chars} chars)")

    if errors:
        for error in errors:
            print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)

    # If no sessions were processed (all skipped), return exit code 2
    if not any_processed:
        print("No sessions found matching criteria", file=sys.stderr)
        sys.exit(2)

    sys.exit(0)


if __name__ == "__main__":
    main()
