#!/usr/bin/env bash
# Capture telnet + WFC sessions and produce PNG screenshots under screenshots/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Ensuring BBS is reachable on :2929..."
if ! timeout 2 bash -c 'cat < /dev/null > /dev/tcp/127.0.0.1/2929' 2>/dev/null; then
  echo "Starting docker compose..."
  docker compose up -d
  sleep 8
fi

echo "==> Telnet session captures..."
./scripts/capture-bbs-screenshots.exp 127.0.0.1 2929

if [[ -f docker/mutineer.wfc.conf ]]; then
  docker build -q -t mutineer-bbs:latest . 2>/dev/null || true
  if ./scripts/capture-wfc-screenshot.sh 2>/dev/null; then
    echo "==> WFC capture OK"
  else
    echo "==> WFC capture skipped (no TTY or docker -it unavailable)"
  fi
fi

echo "==> Rendering PNGs..."
python3 scripts/ansi-to-screenshots.py

echo "==> Screenshots:"
ls -la screenshots/*.png 2>/dev/null || ls -la screenshots/
