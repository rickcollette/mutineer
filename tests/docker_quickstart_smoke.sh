#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"

if ! command -v docker >/dev/null 2>&1; then
  echo "docker not available; skipping quick-start smoke"
  exit 0
fi

if docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE=(docker-compose)
else
  echo "docker compose not available; skipping quick-start smoke"
  exit 0
fi

if [ ! -f docker-compose.yml ] && [ ! -f compose.yml ]; then
  echo "no compose file present; skipping quick-start smoke"
  exit 0
fi

PROJECT="mutineer_smoke_$$_$(date +%s)"
cleanup() {
  "${COMPOSE[@]}" -p "$PROJECT" down -v --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

"${COMPOSE[@]}" -p "$PROJECT" config >/dev/null

if [ "${MUTINEER_DOCKER_FULL_SMOKE:-0}" != "1" ]; then
  echo "compose config validated; set MUTINEER_DOCKER_FULL_SMOKE=1 for container startup"
  exit 0
fi

"${COMPOSE[@]}" -p "$PROJECT" up -d --build
sleep 5
"${COMPOSE[@]}" -p "$PROJECT" ps

if [ -x "$BUILD_DIR/mutineer" ]; then
  "$BUILD_DIR/mutineer" --help >/dev/null
fi

echo "docker quick-start smoke completed"
