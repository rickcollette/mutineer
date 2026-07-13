#!/usr/bin/env bash
# Interactive WFC console (attach TTY). Screenshot with your OS capture tool.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
IMAGE="${MUTINEER_IMAGE:-mutineer-bbs:latest}"
DATA_VOL="${MUTINEER_DATA_VOL:-mutineer-bbs_mutineer-data}"

docker build -t "$IMAGE" . 2>/dev/null || true
echo "Opening WFC on port 2930 (BBS daemon can stay on compose :2929)."
echo "Press Q in WFC to quit. Sysop telnet: telnet localhost 2929"
exec docker run --rm -it \
  -p 2930:2929 \
  -v "${DATA_VOL}:/opt/mutineer/data" \
  -e MUTINEER_CONFIG=conf/mutineer.wfc.conf \
  "$IMAGE"
