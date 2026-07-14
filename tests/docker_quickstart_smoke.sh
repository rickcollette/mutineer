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

COMPOSE_FILE="${MUTINEER_COMPOSE_FILE:-docker/compose.yml}"

if [ ! -f "$COMPOSE_FILE" ]; then
  echo "no compose file present at $COMPOSE_FILE; skipping quick-start smoke"
  exit 0
fi

PROJECT="mutineer_smoke_$$_$(date +%s)"
cleanup() {
  "${COMPOSE[@]}" -f "$COMPOSE_FILE" -p "$PROJECT" down -v --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

"${COMPOSE[@]}" -f "$COMPOSE_FILE" -p "$PROJECT" config >/dev/null

if [ "${MUTINEER_DOCKER_FULL_SMOKE:-0}" != "1" ]; then
  echo "compose config validated; set MUTINEER_DOCKER_FULL_SMOKE=1 for container startup"
  exit 0
fi

"${COMPOSE[@]}" -f "$COMPOSE_FILE" -p "$PROJECT" up -d --build
sleep 5
"${COMPOSE[@]}" -f "$COMPOSE_FILE" -p "$PROJECT" ps

if [ -x "$BUILD_DIR/mutineer" ]; then
  "$BUILD_DIR/mutineer" --help >/dev/null
fi

echo "docker quick-start smoke completed"
