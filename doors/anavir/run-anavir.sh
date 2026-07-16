#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
set -a
. "$ROOT/anavir-mutineer.env"
set +a
exec "$ROOT/bin/mutineermud" "$ANAVIR_PORT" --ipc-socket "$ANAVIR_IPC_SOCKET" --bbs-secret "$ANAVIR_BBS_SECRET"
