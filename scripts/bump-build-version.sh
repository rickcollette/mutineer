#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HEADER="$ROOT/include/bbslib/lifecycle.h"

version="$(sed -n 's/^#define BBSLIB_VERSION_STRING "\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)"$/\1/p' "$HEADER")"
if [[ -z "$version" ]]; then
  echo "ERROR: BBSLIB_VERSION_STRING must use MAJOR.MINOR.BUILD in $HEADER" >&2
  exit 1
fi

IFS=. read -r major minor build <<<"$version"
next_build=$((build + 1))
next_version="$major.$minor.$next_build"
tmp="$(mktemp "${HEADER}.XXXXXX")"
trap 'rm -f "$tmp"' EXIT

awk -v major="$major" -v minor="$minor" -v build="$next_build" -v version="$next_version" '
  /^#define BBSLIB_VERSION_MAJOR / { print "#define BBSLIB_VERSION_MAJOR " major; next }
  /^#define BBSLIB_VERSION_MINOR / { print "#define BBSLIB_VERSION_MINOR " minor; next }
  /^#define BBSLIB_VERSION_PATCH / { print "#define BBSLIB_VERSION_PATCH " build; next }
  /^#define BBSLIB_VERSION_STRING / { print "#define BBSLIB_VERSION_STRING \"" version "\""; next }
  { print }
' "$HEADER" >"$tmp"

chmod --reference="$HEADER" "$tmp"
mv "$tmp" "$HEADER"
trap - EXIT
echo "==> BBSLib build version $version -> $next_version"
