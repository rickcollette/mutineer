#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build-make}"
root="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$build_dir" = /* ]]; then
  bin_prefix="$build_dir"
else
  bin_prefix="$root/$build_dir"
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/mutineer-release-smoke.XXXXXX")"
cleanup() { rm -rf "$tmpdir"; }
trap cleanup EXIT

out="$tmpdir/out"
VERSION=smoke PLATFORM=debian BUILD_DIR="$bin_prefix" OUTPUT_DIR="$out" \
  "$root/scripts/build-release.sh" >/tmp/mutineer-release-smoke.log

tarball="$out/mutineer-smoke-x86_64-debian.tar.gz"
test -s "$tarball"

tar -xzf "$tarball" -C "$tmpdir"
pkg="$tmpdir/mutineer-smoke-x86_64-debian"

for required in \
  "$pkg/mutineer" \
  "$pkg/bin/mutineer-console" \
  "$pkg/bin/mutineer-initbbs" \
  "$pkg/plank/bin/coved" \
  "$pkg/scripts/open-wfc.sh" \
  "$pkg/scripts/update-version" \
  "$pkg/conf/mutineer.conf" \
  "$pkg/sql/schema.sql"; do
  test -e "$required"
done

for script in start stop backup bbs-wall start-screen watchdog update-version open-wfc.sh; do
  bash -n "$pkg/scripts/$script"
done

(
  cd "$pkg"
  ./bin/mutineer-initbbs -c conf/mutineer.conf -y >/tmp/mutineer-release-init.log 2>&1
  test -s data/mutineer.db
)

if command -v sqlite3 >/dev/null 2>&1; then
  (cd "$pkg" && ./scripts/backup >/tmp/mutineer-release-backup.log 2>&1)
  ls "$tmpdir"/mutineer-smoke-x86_64-debian/backups/mutineer_backup_*.tar.gz >/dev/null
else
  echo "sqlite3 CLI not available; packaged backup syntax/layout smoke only"
fi

MUTINEER_UPDATE_URL=https://updates.invalid/mutineer "$pkg/scripts/update-version" --dry-run \
  >/tmp/mutineer-release-update-dry-run.log 2>&1
