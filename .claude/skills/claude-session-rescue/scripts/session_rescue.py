#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# ///
"""
Session Rescue CLI - Strip base64 images from Claude Code session JSONL files.

Usage:
    uv run --script session_rescue.py --session-id <ID> [--dry-run] [--verbose]
    uv run --script session_rescue.py --latest [--dry-run] [--verbose]
    uv run --script session_rescue.py --all [--dry-run] [--verbose]
"""

import argparse
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any


def find_sessions(session_id: str | None = None, session_name: str | None = None,
                  latest: bool = False, all_sessions: bool = False,
                  project_path: str | None = None) -> list[Path]:
    """Find session JSONL files based on search criteria."""
    base_dir = Path.home() / ".claude" / "projects"
    
    if not base_dir.exists():
        return []
    
    results = []
    
    if all_sessions:
        # Find all JSONL files
        for jsonl_file in base_dir.rglob("*.jsonl"):
            results.append(jsonl_file)
    elif latest:
        # Find most recently modified session
        jsonl_files = list(base_dir.rglob("*.jsonl"))
        if jsonl_files:
            results = [max(jsonl_files, key=lambda p: p.stat().st_mtime)]
    elif session_id:
        # Find by UUID prefix match
        for jsonl_file in base_dir.rglob("*.jsonl"):
            if session_id in jsonl_file.stem:
                results.append(jsonl_file)
    elif session_name:
        # Find by custom title - need to check content
        for jsonl_file in base_dir.rglob("*.jsonl"):
            try:
                with open(jsonl_file, 'r', encoding='utf-8') as f:
                    for line in f:
                        try:
                            obj = json.loads(line)
                            if obj.get('type') == 'custom-title':
                                if session_name.lower() in obj.get('title', '').lower():
                                    results.append(jsonl_file)
                                    break
                        except json.JSONDecodeError:
                            continue
            except Exception:
                continue
    
    # Filter by project path if specified
    if project_path and results:
        project_path = Path(project_path).resolve()
        results = [r for r in results if project_path in r.resolve().parents or project_path == r.resolve().parent]
    
    return results


def check_pid_lock(session_path: Path) -> bool:
    """Check if session has an active PID lock file."""
    session_dir = session_path.parent
    # Check for any .pid or lock file in the session directory
    for lock_file in session_dir.glob("*.pid"):
        if lock_file.exists():
            return True
    return False


def count_images_recursive(obj: Any) -> tuple[int, int]:
    """
    Recursively count images in JSON structure.
    Returns (image_count, total_base64_chars).
    """
    image_count = 0
    total_chars = 0
    
    if isinstance(obj, dict):
        # Check if this is an image object
        if obj.get('type') == 'image' and isinstance(obj.get('source'), dict):
            source = obj['source']
            if source.get('type') == 'base64':
                data = source.get('data', '')
                image_count += 1
                total_chars += len(data)
                return image_count, total_chars
        
        # Recurse into all values
        for value in obj.values():
            count, chars = count_images_recursive(value)
            image_count += count
            total_chars += chars
    
    elif isinstance(obj, list):
        for item in obj:
            count, chars = count_images_recursive(item)
            image_count += count
            total_chars += chars
    
    return image_count, total_chars


def strip_images_recursive(obj: Any) -> tuple[Any, int]:
    """
    Recursively strip base64 images from JSON structure.
    Returns (modified_obj, image_count_removed).
    """
    image_count = 0
    
    if isinstance(obj, dict):
        # Check if this is an image object that should be replaced
        if obj.get('type') == 'image' and isinstance(obj.get('source'), dict):
            source = obj['source']
            if source.get('type') == 'base64':
                data = source.get('data', '')
                media_type = source.get('media_type', 'unknown')
                # Replace with placeholder
                placeholder = {
                    'type': 'text',
                    'text': f'[image removed: {media_type}, {len(data)} base64 chars]'
                }
                return placeholder, 1
        
        # Recurse into all values
        new_obj = {}
        for key, value in obj.items():
            new_value, count = strip_images_recursive(value)
            new_obj[key] = new_value
            image_count += count
        
        return new_obj, image_count
    
    elif isinstance(obj, list):
        new_list = []
        for item in obj:
            new_item, count = strip_images_recursive(item)
            new_list.append(new_item)
            image_count += count
        
        return new_list, image_count
    
    return obj, 0


def process_jsonl_line(line: str, verbose: bool = False) -> tuple[str, int, int]:
    """
    Process a single JSONL line.
    Returns (output_line, image_count, bytes_saved).
    """
    line = line.rstrip('\n\r')
    
    if not line.strip():
        return line, 0, 0
    
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        # Malformed line - pass through unchanged
        if verbose:
            print(f"  Warning: Malformed JSON line, passing through", file=sys.stderr)
        return line, 0, 0
    
    # Count images before stripping
    image_count, total_chars = count_images_recursive(obj)
    bytes_saved = total_chars  # Base64 chars ≈ bytes for estimation
    
    if image_count > 0:
        # Strip images
        modified_obj, _ = strip_images_recursive(obj)
        output_line = json.dumps(modified_obj, ensure_ascii=False)
        return output_line, image_count, bytes_saved
    
    return line, 0, 0


def create_backup(session_path: Path) -> Path:
    """Create a backup of the session file."""
    base_backup = Path(str(session_path) + '.bak')
    
    # If .bak doesn't exist, use it
    if not base_backup.exists():
        backup_path = base_backup
    else:
        # Find the next numbered backup
        counter = 1
        while Path(str(session_path) + '.bak.' + str(counter)).exists():
            counter += 1
        backup_path = Path(str(session_path) + '.bak.' + str(counter))
    
    shutil.copy2(session_path, backup_path)
    return backup_path


def atomic_write(session_path: Path, content: str) -> None:
    """Write content atomically using temp file + rename."""
    temp_path = Path(str(session_path) + '.tmp')
    
    try:
        with open(temp_path, 'w', encoding='utf-8') as f:
            f.write(content)
        
        # Preserve original permissions
        if session_path.exists():
            shutil.copymode(session_path, temp_path)
        
        # Atomic rename (os.replace overwrites on Windows; os.rename does not)
        os.replace(temp_path, session_path)
    except Exception:
        # Clean up temp file on failure
        if temp_path.exists():
            temp_path.unlink()
        raise


def process_session(session_path: Path, dry_run: bool = False, verbose: bool = False) -> dict:
    """
    Process a session JSONL file.
    Returns dict with processing results.
    """
    result = {
        'path': str(session_path),
        'images_found': 0,
        'images_removed': 0,
        'bytes_saved': 0,
        'backup_path': None,
        'dry_run': dry_run,
        'error': None
    }
    
    if not session_path.exists():
        result['error'] = 'Session file not found'
        return result
    
    # Check for active session
    if check_pid_lock(session_path):
        result['error'] = 'Session has active PID lock - skipping'
        return result
    
    # Create backup first
    try:
        backup_path = create_backup(session_path)
        result['backup_path'] = str(backup_path)
    except Exception as e:
        result['error'] = f'Backup failed: {e}'
        return result
    
    if verbose:
        print(f"  Backup created: {backup_path}")
    
    # Process line by line
    modified_lines = []
    total_images = 0
    total_bytes = 0
    
    try:
        with open(session_path, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                output_line, img_count, bytes_saved = process_jsonl_line(line, verbose)
                modified_lines.append(output_line)
                total_images += img_count
                total_bytes += bytes_saved
    except Exception as e:
        result['error'] = f'Read failed: {e}'
        return result
    
    result['images_found'] = total_images
    result['bytes_saved'] = total_bytes
    
    if total_images == 0:
        if verbose:
            print(f"  No images found in {session_path}")
        return result
    
    if dry_run:
        if verbose:
            print(f"  Dry run: would remove {total_images} images ({total_bytes} chars)")
        return result
    
    # Write modified content
    try:
        atomic_write(session_path, '\n'.join(modified_lines) + '\n')
        result['images_removed'] = total_images
        if verbose:
            print(f"  Removed {total_images} images from {session_path}")
    except Exception as e:
        result['error'] = f'Write failed: {e}'
        return result
    
    return result


def main():
    parser = argparse.ArgumentParser(
        description='Strip base64 images from Claude Code session JSONL files.'
    )
    
    # Session selection (mutually exclusive)
    session_group = parser.add_mutually_exclusive_group()
    session_group.add_argument('--session-id', metavar='ID',
                               help='Find session by UUID prefix match')
    session_group.add_argument('--session-name', metavar='NAME',
                               help='Find session by custom title')
    session_group.add_argument('--latest', action='store_true',
                               help='Process most recently modified session')
    session_group.add_argument('--all', action='store_true', dest='all_sessions',
                               help='Process all sessions')
    
    # Options
    parser.add_argument('--project', metavar='PATH',
                        help='Scope search to project directory')
    parser.add_argument('--dry-run', action='store_true',
                        help='Report images found without modifying')
    parser.add_argument('--min-images', type=int, default=1, metavar='N',
                        help='Only process sessions with >= N images (default: 1)')
    parser.add_argument('--verbose', action='store_true',
                        help='Detailed output')
    
    args = parser.parse_args()
    
    # Validate that at least one session selection is provided
    if not any([args.session_id, args.session_name, args.latest, args.all_sessions]):
        parser.error('One of --session-id, --session-name, --latest, or --all is required')
    
    # Find sessions
    if args.verbose:
        print(f"Searching for sessions...")
    
    sessions = find_sessions(
        session_id=args.session_id,
        session_name=args.session_name,
        latest=args.latest,
        all_sessions=args.all_sessions,
        project_path=args.project
    )
    
    if not sessions:
        print("No sessions found matching criteria", file=sys.stderr)
        sys.exit(2)
    
    if args.verbose:
        print(f"Found {len(sessions)} session(s)")
    
    # Process each session
    total_images = 0
    total_bytes = 0
    error_count = 0
    
    for session_path in sessions:
        if args.verbose:
            print(f"\nProcessing: {session_path}")
        
        result = process_session(session_path, args.dry_run, args.verbose)
        
        if result['error']:
            print(f"  Error: {result['error']}", file=sys.stderr)
            error_count += 1
            continue
        
        # Check min-images threshold
        if result['images_found'] < args.min_images:
            if args.verbose:
                print(f"  Skipping: only {result['images_found']} images (threshold: {args.min_images})")
            continue
        
        total_images += result['images_found']
        total_bytes += result['bytes_saved']
        
        if args.dry_run:
            print(f"  {result['path']}: {result['images_found']} images ({result['bytes_saved']} chars)")
        else:
            print(f"  {result['path']}: removed {result['images_removed']} images ({result['bytes_saved']} chars)")
            print(f"  Backup: {result['backup_path']}")
    
    # Summary
    print(f"\n{'Dry run: ' if args.dry_run else ''}Total: {total_images} images removed ({total_bytes} chars)")
    
    if error_count > 0:
        print(f"Errors: {error_count}", file=sys.stderr)
        sys.exit(1)
    
    sys.exit(0)


if __name__ == '__main__':
    main()
