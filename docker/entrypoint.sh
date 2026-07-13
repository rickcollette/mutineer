#!/bin/bash
set -euo pipefail
cd /opt/mutineer

CONF="${MUTINEER_CONFIG:-conf/mutineer.docker.conf}"

mkdir -p data logs data/dropfiles data/files plugins

if [[ ! -f data/mutineer.db ]]; then
  echo "==> First run: initializing database..."
  ./bin/mutineer-initbbs -c "$CONF" -y
fi

echo "==> Starting Mutineer BBS on port 2929..."
exec ./mutineer -c "$CONF"
