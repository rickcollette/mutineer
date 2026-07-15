#!/usr/bin/env bash
# Capture WFC console ANSI via mutineer-console, then render PNG.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
RAW="$ROOT/screenshots/raw"
mkdir -p "$RAW"

CONSOLE="${MUTINEER_CONSOLE:-build-make/mutineer-console}"
CONF="${MUTINEER_CONFIG:-conf/mutineer.conf}"
OUT="$RAW/05-wfc-console.ansi"

echo "==> Capturing WFC console (requires console service and TTY)..."
if [[ ! -x "$CONSOLE" ]]; then
  echo "mutineer-console not found at $CONSOLE" >&2
  exit 1
fi

# Record terminal output from a short standalone console session.
script -q -c \
  "timeout 4 ${CONSOLE} -c ${CONF}" \
  "$OUT" 2>/dev/null || true

# script may leave CRLF; trim expect noise
if [[ -f "$OUT" ]]; then
  sed -i 's/\r$//' "$OUT" 2>/dev/null || sed -i '' 's/\r$//' "$OUT" 2>/dev/null || true
  # Drop script(1) boilerplate lines
  grep -v '^Script started' "$OUT" | grep -v '^Script done' > "${OUT}.tmp" && mv "${OUT}.tmp" "$OUT" || true
  echo "WFC capture: $(wc -c < "$OUT") bytes -> $OUT"
else
  echo "WFC capture failed — try: ./scripts/open-wfc.sh" >&2
  exit 1
fi
