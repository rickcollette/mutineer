#!/bin/bash
# Deploy website/ to gh-pages branch for GitHub Pages.
# Uses a temporary clone — never rsync --delete on the repo root.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "Building HTML from markdown..."
python3 scripts/build-website.py

DEPLOY_DIR=$(mktemp -d)
trap 'rm -rf "$DEPLOY_DIR"' EXIT

echo "Cloning gh-pages into temp directory..."
git clone --branch gh-pages --single-branch --depth 1 \
  "$(git remote get-url origin)" "$DEPLOY_DIR"

echo "Syncing website files..."
find "$DEPLOY_DIR" -mindepth 1 -maxdepth 1 ! -name '.git' -exec rm -rf {} +
cp -a "$ROOT/website/." "$DEPLOY_DIR/"

cd "$DEPLOY_DIR"
git add -A
if git diff --cached --quiet; then
  echo "No changes to deploy."
else
  git commit -m "Update documentation website"
  git push origin gh-pages
  echo "Deployed to https://rickcollette.github.io/mutineer/"
fi
