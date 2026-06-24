#!/usr/bin/env bash
# grok-review.sh — run an independent, READ-ONLY Grok review of a diff.
# Separate billing (grok.com), NOT Claude usage. See ../SKILL.md.
#
# Usage:
#   grok-review.sh working                # review UNCOMMITTED changes (git diff HEAD)  <-- default use here
#   grok-review.sh <branch>               # review main...<branch> (origin/main if it resolves, else local main)
#   grok-review.sh <baseSha>..            # review <baseSha>..HEAD (trailing .. = "..HEAD")
#   grok-review.sh <baseSha>...<headSha>  # review an explicit range
#   grok-review.sh <pr-number>            # review a GitHub PR diff (needs a remote + gh)
#   grok-review.sh <pr-number> --post     # Grok posts its review onto the PR (gh pr comment); PR# only
#
# Env: GROK_MODEL (default: grok's default), GROK_EFFORT (default: unset — only for a reasoning model),
#      GROK_TIMEOUT (default 420). Output: prints the review to stdout; full JSON to $OUT (/tmp/grok-review.json).
set -euo pipefail

TARGET="${1:?usage: grok-review.sh <working|branch|baseSha..|range|pr#> [--post]}"
POST=0; for a in "$@"; do [[ "$a" == "--post" ]] && POST=1; done
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CRITERIA="$HERE/../references/review-prompt.md"
OUT="${OUT:-/tmp/grok-review.json}"
PROMPT="${PROMPT_FILE:-/tmp/grok-review-prompt.txt}"
EFFORT="${GROK_EFFORT:-}"           # opt-in; default model (grok-composer-2.5-fast) rejects reasoningEffort
TIMEOUT="${GROK_TIMEOUT:-420}"

command -v grok >/dev/null || { echo "grok CLI not found (~/.grok/bin/grok)"; exit 127; }
# Auth check via a here-string (piping into grep -q under pipefail can SIGPIPE grok -> false failure).
MODELS_OUT="$(grok models 2>/dev/null || true)"
grep -qiE "logged in|composer|grok-build" <<<"$MODELS_OUT" \
  || { echo "grok unreachable or not logged in — run 'grok models'; if needed ask the user to 'grok login'"; exit 1; }

# --- resolve the diff ---
if [[ "$TARGET" == "working" || "$TARGET" == "wt" || "$TARGET" == "." ]]; then
  DIFF="$(git diff HEAD)"; LABEL="working tree (uncommitted vs HEAD)"
elif [[ "$TARGET" =~ ^[0-9]+$ ]]; then
  DIFF="$(gh pr diff "$TARGET")"; LABEL="PR #$TARGET"
elif [[ "$TARGET" == *"..."* ]]; then
  DIFF="$(git diff "$TARGET")"; LABEL="range $TARGET"
elif [[ "$TARGET" == *".." ]]; then
  DIFF="$(git diff "${TARGET}HEAD")"; LABEL="${TARGET}HEAD"
else
  git fetch origin -q 2>/dev/null || true
  BASE="main"; git rev-parse --verify -q origin/main >/dev/null 2>&1 && BASE="origin/main"
  DIFF="$(git diff "$BASE...$TARGET")"; LABEL="$BASE...$TARGET"
fi
[[ -n "$DIFF" ]] || { echo "empty diff for $LABEL"; exit 1; }

# --- build the self-contained prompt (criteria + diff) ---
{ cat "$CRITERIA"; printf '%s\n' "$DIFF"; printf '\n===== END DIFF =====\n'; } > "$PROMPT"
echo ">> reviewing $LABEL ($(wc -c < "$PROMPT") bytes) with grok (effort=${EFFORT:-default}, read-only)..." >&2

MODEL_ARG=(); [[ -n "${GROK_MODEL:-}" ]] && MODEL_ARG=(--model "$GROK_MODEL")
EFFORT_ARG=(); [[ -n "$EFFORT" ]] && EFFORT_ARG=(--effort "$EFFORT")

if [[ "$POST" == "1" ]]; then
  [[ "$TARGET" =~ ^[0-9]+$ ]] || { echo "--post requires a PR NUMBER as the target"; exit 2; }
  printf '\nPUBLISH: post the review above as a comment on PR #%s — write your findings to a temp file UNDER /tmp (e.g. /tmp/grok-pr%s-review.md, NOT in the repo) and run `gh pr comment %s --body-file /tmp/grok-pr%s-review.md`. Use `gh` ONLY to post this one comment; do NOT create files in the repo, edit/commit/push code, or merge/close the PR.\n' "$TARGET" "$TARGET" "$TARGET" "$TARGET" >> "$PROMPT"
  echo ">> POST mode: Grok will comment on PR #$TARGET (gh allowed, edits denied)." >&2
  timeout "$TIMEOUT" grok --prompt-file "$PROMPT" \
    --output-format json --disable-web-search \
    --allow "Bash(gh*)" --disallowed-tools "Edit,Write,MultiEdit,NotebookEdit" \
    "${EFFORT_ARG[@]}" "${MODEL_ARG[@]}" \
    2>/tmp/grok-review-err.txt > "$OUT" || { echo "grok review/post failed (see /tmp/grok-review-err.txt)"; tail -5 /tmp/grok-review-err.txt; exit 1; }
else
  timeout "$TIMEOUT" grok --prompt-file "$PROMPT" \
    --output-format json --permission-mode plan --disable-web-search \
    "${EFFORT_ARG[@]}" "${MODEL_ARG[@]}" \
    2>/tmp/grok-review-err.txt > "$OUT" || { echo "grok review failed (see /tmp/grok-review-err.txt)"; tail -5 /tmp/grok-review-err.txt; exit 1; }
fi

# --- emit the review text; guarantee read-only ---
node -e 'process.stdout.write((JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")).text)||"")' "$OUT" 2>/dev/null || cat "$OUT"
echo
if [[ -n "$(git status --porcelain 2>/dev/null)" ]]; then
  echo "!! NOTE: working tree was already dirty (expected if reviewing 'working'). A review run itself must not ADD changes." >&2
fi
