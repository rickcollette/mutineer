#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="${VERSION:-dev}"
BUILD_DIR="${BUILD_DIR:-build-dist}"
DIST_ROOT="${DIST_ROOT:-dist}"
STAGING="$DIST_ROOT/mutineer"
TARBALL="$DIST_ROOT/mutineer.tar"
INSTALLER="$DIST_ROOT/mutineer-install"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
JOBS="${JOBS:-2}"

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
  mutineer-rest
)
PLANK_BIN=(
  plankd
  coved
  plankctl
  plankpack
  plank-offline
)
PLUGIN_BIN=(
  chat_plugin.so
  hello.so
)

log() { echo "==> $*"; }
die() { echo "package.sh: $*" >&2; exit 1; }

require_exec() {
  [ -x "$1" ] || die "missing executable: $1"
}

require_file() {
  [ -f "$1" ] || die "missing file: $1"
}

log "Configuring Mutineer release build in $BUILD_DIR"
rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,\\\$ORIGIN/lib:\\\$ORIGIN/../lib"

log "Building Mutineer release targets"
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target \
  mutineer tools plank plugins bucc

for bin in "${CORE_BIN[@]}" "${TOOL_BIN[@]}" "${PLANK_BIN[@]}"; do
  require_exec "$BUILD_DIR/$bin"
done
require_exec "$BUILD_DIR/bucc"
for plugin in "${PLUGIN_BIN[@]}"; do
  require_file "$BUILD_DIR/plugins/$plugin"
done

log "Staging package"
rm -rf "$STAGING" "$TARBALL" "$INSTALLER"
mkdir -p "$STAGING/bin" "$STAGING/plank/bin" "$STAGING/plugins" "$STAGING/lib" \
  "$STAGING/conf" "$STAGING/art" "$STAGING/menus" "$STAGING/sql" \
  "$STAGING/scripts" "$STAGING/data" "$STAGING/logs" "$STAGING/doors" \
  "$STAGING/docs" "$STAGING/buccaneer/bin" "$STAGING/buccaneer/include/buccaneer" \
  "$STAGING/buccaneer/examples"

install -m 0755 "$BUILD_DIR/mutineer" "$STAGING/mutineer"
install -m 0755 "$BUILD_DIR/bucc" "$STAGING/buccaneer/bin/bucc"
for tool in "${TOOL_BIN[@]}"; do
  install -m 0755 "$BUILD_DIR/$tool" "$STAGING/bin/$tool"
done
for tool in "${PLANK_BIN[@]}"; do
  install -m 0755 "$BUILD_DIR/$tool" "$STAGING/plank/bin/$tool"
done

for plugin in "${PLUGIN_BIN[@]}"; do
  install -m 0755 "$BUILD_DIR/plugins/$plugin" "$STAGING/plugins/$plugin"
done

if compgen -G "/usr/local/lib/libnotcurses-core.so*" >/dev/null; then
  install -m 0755 /usr/local/lib/libnotcurses-core.so* "$STAGING/lib/"
fi

# notcurses is built from source in the compatibility image.  Bundle its
# non-glibc runtime closure as well; minimal target hosts do not necessarily
# provide these SONAMEs (notably libunistring.so.2).
for soname in libtinfo.so.6 libunistring.so.2 libdeflate.so.0; do
  resolved="$(ldd /usr/local/lib/libnotcurses-core.so | awk -v name="$soname" '$1 == name && $2 == "=>" { print $3; exit }')"
  [ -n "$resolved" ] || die "unable to resolve notcurses runtime library: $soname"
  require_file "$resolved"
  install -m 0755 "$resolved" "$STAGING/lib/$soname"
done

# The compatibility build may link OpenSSL 1.1 even when the target host only
# ships OpenSSL 3. Bundle the exact SONAMEs selected by the linker. The release
# RPATH above lets both root-level and bin/ executables find them in lib/.
for soname in libssl.so.1.1 libcrypto.so.1.1; do
  resolved="$(ldd "$BUILD_DIR/mutineer-rest" | awk -v name="$soname" '$1 == name && $2 == "=>" { print $3; exit }')"
  [ -n "$resolved" ] || resolved="$(ldd "$BUILD_DIR/mutineer" | awk -v name="$soname" '$1 == name && $2 == "=>" { print $3; exit }')"
  [ -n "$resolved" ] || die "unable to resolve required runtime library: $soname"
  require_file "$resolved"
  install -m 0755 "$resolved" "$STAGING/lib/$soname"
done

cp -a conf/. "$STAGING/conf/"
cp -a art/. "$STAGING/art/"
cp -a menus/. "$STAGING/menus/"
cp -a sql/. "$STAGING/sql/"
cp -a docs/. "$STAGING/docs/"
cp -a src/buccaneer/include/. "$STAGING/buccaneer/include/buccaneer/"
if [ -d src/buccaneer/examples ]; then
  cp -a src/buccaneer/examples/. "$STAGING/buccaneer/examples/"
fi
install -m 0644 README.md "$STAGING/README.md"
install -m 0644 LICENSE "$STAGING/LICENSE"
install -m 0644 LICENSE "$STAGING/docs/LICENSE"

RUNTIME_SCRIPTS=(start stop backup bbs-wall start-screen crontabs watchdog update-version open-wfc.sh)
for script in "${RUNTIME_SCRIPTS[@]}"; do
  if [ -f "scripts/$script" ]; then
    install -m 0755 "scripts/$script" "$STAGING/scripts/$script"
  fi
done

if [ -d doors/testdoor ]; then
  cp -a doors/testdoor "$STAGING/doors/"
fi

cat >"$STAGING/VERSION" <<EOF
version=$VERSION
platform=linux-x86_64-glibc228
built=$(date -u +%Y-%m-%dT%H:%M:%SZ)
commit=${GITHUB_SHA:-local}
EOF

cat >"$STAGING/INSTALL.md" <<EOF
# Mutineer BBS $VERSION

This package was built for Linux x86_64 compatibility with glibc 2.28 or newer.

## Install

\`\`\`sh
chmod 755 mutineer-install
./mutineer-install --prefix /srv/mutineer
./mutineer-install --prefix /srv/mutineer --check
\`\`\`

The installer can install missing runtime dependencies on Debian, Ubuntu,
Rocky Linux, AlmaLinux, RHEL, Fedora, Alpine Linux, and SUSE/openSUSE.
EOF

build_glibc="$(ldd --version 2>&1 || true)"
build_glibc="${build_glibc%%$'\n'*}"
{
  echo "name=mutineer"
  echo "version=$VERSION"
  echo "platform=linux-x86_64-glibc228"
  echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "commit=${GITHUB_SHA:-local}"
  echo "build_glibc=$build_glibc"
  find "$STAGING" -type f -perm -111 -print | sort | while read -r file; do
    rel="${file#$STAGING/}"
    sha="$(sha256sum "$file" | awk '{print $1}')"
    echo "sha256.$rel=$sha"
  done
} >"$STAGING/MANIFEST"

log "Checking GLIBC symbol requirements"
glibc_versions="$(strings "$STAGING/mutineer" "$STAGING/bin/"* "$STAGING/plank/bin/"* "$STAGING/lib/"* 2>/dev/null | \
  grep -Eo 'GLIBC_[0-9]+\.[0-9]+' | sort -Vu || true)"
if printf '%s\n' "$glibc_versions" | \
  awk -F_ 'BEGIN{bad=0} {split($2,v,"."); if (v[1] > 2 || (v[1] == 2 && v[2] > 28)) bad=1} END{exit bad}'; then
  :
else
  die "release binaries require a glibc newer than 2.28"
fi

log "Creating $TARBALL"
mkdir -p "$DIST_ROOT"
tar -C "$DIST_ROOT" -cf "$TARBALL" mutineer

log "Creating $INSTALLER"
bash packaging/make-self-extracting-installer.sh \
  packaging/setup.sh "$TARBALL" "$INSTALLER"

log "Package complete:"
du -h "$TARBALL" "$INSTALLER"
