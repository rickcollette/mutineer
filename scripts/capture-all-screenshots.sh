#!/usr/bin/env bash
# Capture telnet + WFC console sessions and produce PNG screenshots under screenshots/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Ensuring BBS is reachable on :2929..."
if ! timeout 2 bash -c 'cat < /dev/null > /dev/tcp/127.0.0.1/2929' 2>/dev/null; then
  echo "Starting docker compose..."
  docker compose -f docker/compose.yml up -d
  sleep 8
fi

echo "==> Telnet session captures..."
./scripts/capture-bbs-screenshots.exp 127.0.0.1 2929

if ./scripts/capture-wfc-screenshot.sh 2>/dev/null; then
  echo "==> WFC capture OK"
else
  echo "==> WFC capture skipped (console service unavailable or no TTY)"
fi

echo "==> Rendering PNGs..."
python3 scripts/ansi-to-screenshots.py

echo "==> Screenshots:"
ls -la screenshots/*.png 2>/dev/null || ls -la screenshots/
