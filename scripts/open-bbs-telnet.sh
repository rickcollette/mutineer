#!/usr/bin/env bash
# Interactive telnet session to the running BBS (sysop / mutineer).
set -euo pipefail
HOST="${BBS_HOST:-127.0.0.1}"
PORT="${BBS_PORT:-2929}"
echo "Telnet ${HOST}:${PORT}  (login: sysop / mutineer)"
exec telnet "$HOST" "$PORT"
