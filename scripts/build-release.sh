#!/bin/bash
# Package a Mutineer BBS release tarball from a completed CMake build.
#
# Usage:
#   VERSION=1.0.0 PLATFORM=debian BUILD_DIR=build OUTPUT_DIR=dist ./scripts/build-release.sh
#
# PLATFORM values: debian | fedora | alpine
#   debian  — for Debian 12+ and Ubuntu 24.04+ (glibc, built on Debian Bookworm)
#   fedora  — for Fedora 39+ (glibc)
#   alpine  — for Alpine 3.18+ (musl)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="${VERSION:-dev}"
PLATFORM="${PLATFORM:-debian}"
BUILD_DIR="${BUILD_DIR:-build}"
OUTPUT_DIR="${OUTPUT_DIR:-dist/releases}"

# Binaries to ship
CORE_BIN=(mutineer)
TOOL_BIN=(
  mutineer-console
  mutineer-initbbs
  mutineer-qwkgen
  mutineer-msgpack
  mutineer-userpack
  mutineer-filepack
  mutineer-stats
  mutineer-maint
  mutineer-validate
  mutineer-netmail-export
)
PLANK_BIN=(
  plankd
  coved
  plankctl
  plankpack
  plank-offline
)

log() { echo "==> $*"; }

require_binary() {
  local bin="$BUILD_DIR/$1"
  if [[ ! -x "$bin" ]]; then
    echo "ERROR: missing binary $bin — run cmake build first" >&2
    exit 1
  fi
}

for b in "${CORE_BIN[@]}" "${TOOL_BIN[@]}" "${PLANK_BIN[@]}"; do
  require_binary "$b"
done

log "Staging $PLATFORM release v$VERSION"
ARCHIVE_NAME="mutineer-${VERSION}-x86_64-${PLATFORM}"
STAGING="$OUTPUT_DIR/$ARCHIVE_NAME"
rm -rf "$STAGING"
mkdir -p "$STAGING/bin" "$STAGING/plank/bin" "$STAGING/plugins" \
         "$STAGING/conf" "$STAGING/art" "$STAGING/menus" "$STAGING/sql" \
         "$STAGING/scripts" "$STAGING/data" "$STAGING/logs" "$STAGING/doors"

# Core daemon at package root (matches scripts/start expectation)
cp "$BUILD_DIR/mutineer" "$STAGING/mutineer"
chmod 755 "$STAGING/mutineer"

# Maintenance tools
for tool in "${TOOL_BIN[@]}"; do
  cp "$BUILD_DIR/$tool" "$STAGING/bin/"
done

# PLANK tools
for tool in "${PLANK_BIN[@]}"; do
  cp "$BUILD_DIR/$tool" "$STAGING/plank/bin/"
done

# Sample plugins
if compgen -G "$BUILD_DIR/plugins/*.so" > /dev/null; then
  cp "$BUILD_DIR/plugins/"*.so "$STAGING/plugins/"
fi

# Runtime data (not doors with large binaries — sample only)
cp -a conf/. "$STAGING/conf/"
cp -a art/. "$STAGING/art/"
cp -a menus/. "$STAGING/menus/"
cp -a sql/. "$STAGING/sql/"

# Runtime helper scripts only (exclude build/CI tooling)
RUNTIME_SCRIPTS=(start stop backup bbs-wall start-screen crontabs watchdog update-version open-wfc.sh)
mkdir -p "$STAGING/scripts"
for s in "${RUNTIME_SCRIPTS[@]}"; do
  [[ -f "scripts/$s" ]] && cp "scripts/$s" "$STAGING/scripts/"
done
chmod +x "$STAGING/scripts/"* 2>/dev/null || true
cp README.md LICENSE "$STAGING/" 2>/dev/null || true

# Sample test door (small)
if [[ -d doors/testdoor ]]; then
  cp -a doors/testdoor "$STAGING/doors/"
fi

# Version metadata
cat > "$STAGING/VERSION" <<EOF
version=$VERSION
platform=$PLATFORM
built=$(date -u +%Y-%m-%dT%H:%M:%SZ)
commit=${GITHUB_SHA:-local}
EOF

# Platform-specific install notes
case "$PLATFORM" in
  debian)
    DEPS="libsqlite3-0 libssl3 libarchive13 sqlite3 dosbox (optional, for DOS doors)"
    DISTRO_NOTE="Debian 12 (Bookworm) and Ubuntu 24.04 LTS or newer"
    INSTALL_CMD="sudo apt-get install -y libsqlite3-0 libssl3 libarchive13 sqlite3 dosbox"
    ;;
  fedora)
    DEPS="sqlite-libs openssl-libs libarchive sqlite dosbox (optional)"
    DISTRO_NOTE="Fedora 39 or newer"
    INSTALL_CMD="sudo dnf install -y sqlite openssl-libs libarchive dosbox"
    ;;
  alpine)
    DEPS="sqlite-libs libcrypto3 libarchive-tools sqlite dosbox (optional)"
    DISTRO_NOTE="Alpine 3.18 or newer"
    INSTALL_CMD="sudo apk add sqlite-libs libcrypto3 libarchive-tools sqlite dosbox"
    ;;
  *)
    echo "Unknown PLATFORM: $PLATFORM" >&2
    exit 1
    ;;
esac

cat > "$STAGING/INSTALL.md" <<EOF
# Mutineer BBS $VERSION — $PLATFORM

Pre-built release for **$DISTRO_NOTE** (x86_64).

## Runtime dependencies

$DEPS

Install on target system:

\`\`\`bash
$INSTALL_CMD
\`\`\`

Optional: \`libargon2\` / \`libzstd\` for Argon2 password upgrade and PLANK compression.

## Quick start

\`\`\`bash
tar xzf mutineer-${VERSION}-x86_64-${PLATFORM}.tar.gz
cd mutineer-${VERSION}-x86_64-${PLATFORM}

# Initialize database and directories
./bin/mutineer-initbbs -c conf/mutineer.conf -y

# Start BBS (telnet port 2929)
./mutineer -c conf/mutineer.conf

# Connect
telnet localhost 2929
\`\`\`

## Layout

| Path | Purpose |
|------|---------|
| \`mutineer\` | Main BBS daemon |
| \`bin/\` | Maintenance CLI tools |
| \`plank/bin/\` | PLANK networking tools |
| \`plugins/\` | Sample loadable plugins |
| \`conf/\` | Configuration |
| \`menus/\`, \`art/\` | Menu and display files |
| \`sql/\` | Database schema |
| \`scripts/\` | Helper scripts |

## Documentation

https://rickcollette.github.io/mutineer/docs/getting-started.html
EOF

for required in \
  "$STAGING/mutineer" \
  "$STAGING/bin/mutineer-console" \
  "$STAGING/bin/mutineer-initbbs" \
  "$STAGING/plank/bin/coved" \
  "$STAGING/scripts/open-wfc.sh" \
  "$STAGING/sql/schema.sql" \
  "$STAGING/conf/mutineer.conf"; do
  if [[ ! -e "$required" ]]; then
    echo "ERROR: release package missing required file: ${required#$STAGING/}" >&2
    exit 1
  fi
done

# Create tarball
mkdir -p "$OUTPUT_DIR"
TARBALL="$OUTPUT_DIR/${ARCHIVE_NAME}.tar.gz"

log "Creating $TARBALL"
tar -C "$OUTPUT_DIR" -czf "$TARBALL" "$ARCHIVE_NAME"

# Checksums
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$TARBALL" | tee "$TARBALL.sha256"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$TARBALL" | tee "$TARBALL.sha256"
fi

log "Release ready: $TARBALL ($(du -h "$TARBALL" | cut -f1))"
