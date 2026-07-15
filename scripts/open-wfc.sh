#!/usr/bin/env bash
# Interactive WFC console using the standalone mutineer-console client.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONFIG="${MUTINEER_CONFIG:-conf/mutineer.conf}"
CONSOLE="${MUTINEER_CONSOLE:-build-make/mutineer-console}"

if [[ ! -x "$CONSOLE" ]]; then
  echo "mutineer-console not found at $CONSOLE"
  echo "Build it first: cmake --build build-make --target mutineer-console"
  exit 1
fi

exec "$CONSOLE" -c "$CONFIG" "$@"
