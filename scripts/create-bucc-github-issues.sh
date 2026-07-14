#!/usr/bin/env bash
# Create GitHub issues for current Buccaneer follow-up candidates only.
set -euo pipefail

REPO="${MUTINEER_GITHUB_REPO:-rickcollette/mutineer}"
SRC="${BUCC_TODO_FILE:-docs/status/BUCC_TODO.md}"
MODE="${1:-create}"

if [[ "$MODE" != "create" && "$MODE" != "--dry-run" ]]; then
  echo "Usage: $0 [create|--dry-run]" >&2
  exit 2
fi

if [[ ! -f "$SRC" ]]; then
  echo "Missing $SRC" >&2
  exit 1
fi

completed_ids=(
  BUG-1 BUG-2 BUG-3 BUG-4 BUG-5 BUG-6 BUG-7 BUG-8
  MISSING-1 MISSING-2 MISSING-3 MISSING-4
  WIRE-1 WIRE-2 WIRE-3
  SEC-1 SEC-2 SEC-3
)

body="$(awk '
  /^## Current Follow-Up Candidates/ { in_section=1; next }
  /^## / && in_section { exit }
  in_section { print }
' "$SRC")"

for id in "${completed_ids[@]}"; do
  if grep -Eq "(^|[^A-Za-z0-9_-])${id}([^A-Za-z0-9_-]|$)" <<<"$body"; then
    echo "Refusing to file archived completed Buccaneer issue id: $id" >&2
    exit 1
  fi
done

mapfile -t candidates < <(grep -E '^- `[^`]+` ' <<<"$body" || true)
if [[ "${#candidates[@]}" -eq 0 ]]; then
  echo "No current Buccaneer follow-up candidates found in $SRC."
  exit 0
fi

if [[ "$MODE" == "create" ]]; then
  command -v gh >/dev/null 2>&1 || {
    echo "GitHub CLI not found. Run with --dry-run to preview issues." >&2
    exit 1
  }
  gh label create buccaneer --repo "$REPO" --color "1d76db" \
    --description "Buccaneer interpreted language runtime and host APIs" 2>/dev/null || true
fi

for item in "${candidates[@]}"; do
  api="$(sed -E 's/^- `([^`]+)`.*/\1/' <<<"$item")"
  text="$(sed -E 's/^- `[^`]+`[[:space:]]*//' <<<"$item")"
  title="[BUCC] Follow-up: ${api}"
  issue_body="$(cat <<EOF
Current Buccaneer follow-up candidate from \`$SRC\`:

\`$api\` $text

Buccaneer is Mutineer's interpreted language for addons, games, doors, and extensions. Completed archived audit findings are intentionally excluded by this script.
EOF
)"
  if [[ "$MODE" == "--dry-run" ]]; then
    printf 'DRY RUN: %s\n%s\n\n' "$title" "$issue_body"
  else
    gh issue create --repo "$REPO" --title "$title" --label "enhancement,buccaneer" --body "$issue_body"
  fi
done
