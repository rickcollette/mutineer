#!/usr/bin/env bash
# Capture WFC console ANSI via docker TTY, then render PNG.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
RAW="$ROOT/screenshots/raw"
mkdir -p "$RAW"

IMAGE="${MUTINEER_IMAGE:-mutineer-bbs:latest}"
VOL="${MUTINEER_DATA_VOL:-mutineer-bbs_mutineer-data}"
OUT="$RAW/05-wfc-console.ansi"

echo "==> Capturing WFC console (requires -it TTY, port 2930)..."
# Record terminal output from a short WFC session (separate port from compose).
script -q -c \
  "timeout 4 docker run --rm -it \
    -p 2930:2929 \
    -v ${VOL}:/opt/mutineer/data \
    -e MUTINEER_CONFIG=conf/mutineer.wfc.conf \
    ${IMAGE}" \
  "$OUT" 2>/dev/null || true

# script may leave CRLF; trim expect noise
if [[ -f "$OUT" ]]; then
  sed -i 's/\r$//' "$OUT" 2>/dev/null || sed -i '' 's/\r$//' "$OUT" 2>/dev/null || true
  # Drop script(1) boilerplate lines
  grep -v '^Script started' "$OUT" | grep -v '^Script done' > "${OUT}.tmp" && mv "${OUT}.tmp" "$OUT" || true
  echo "WFC capture: $(wc -c < "$OUT") bytes -> $OUT"
else
  echo "WFC capture failed — try: docker run -it -e MUTINEER_CONFIG=conf/mutineer.wfc.conf ${IMAGE}" >&2
  exit 1
fi
