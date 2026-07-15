#!/usr/bin/env bash
# PNG screenshots: expect telnet captures + headless render; WFC via mutineer-console + render.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$ROOT/screenshots"
mkdir -p screenshots/raw

if ! timeout 2 bash -c 'cat < /dev/null > /dev/tcp/127.0.0.1/2929' 2>/dev/null; then
  echo "==> Starting BBS..."
  docker compose -f docker/compose.yml up -d
  sleep 6
fi

echo "==> Telnet sessions (sysop)..."
expect -f scripts/capture-bbs-screenshots.exp 127.0.0.1 2929

echo "==> Render telnet PNGs..."
python3 scripts/ansi-to-screenshots.py

echo "==> WFC console..."
if ./scripts/capture-wfc-screenshot.sh 2>/dev/null; then
  python3 scripts/ansi-to-screenshots.py
fi

# Rename WFC png if rendered from ansi name
[[ -f screenshots/05-wfc-console.png ]] || \
  [[ -f screenshots/05-wfc-console.html ]] && python3 scripts/ansi-to-screenshots.py || true

echo ""
echo "PNG files:"
ls -la "$OUT"/*.png 2>/dev/null || ls -la screenshots/*.png 2>/dev/null || true
