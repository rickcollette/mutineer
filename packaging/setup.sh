#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARBALL="$SCRIPT_DIR/mutineer.tar"
PAYLOAD_DIR=""
PREFIX="$HOME/mutineer"
CONFIG=""
DB_PATH=""
DATA_DIR=""
LOGS_DIR=""
DOORS_DIR=""
RUNTIME_DIR=""
DROPFILE_DIR=""
PORT=""
RUN_USER=""
RUN_GROUP=""
CHECK_ONLY=0
INSTALL_DEPS=1
CONFIG_SET=0
DB_SET=0
DATA_SET=0
LOGS_SET=0
DOORS_SET=0
RUNTIME_SET=0
PORT_SET=0

usage() {
  cat <<EOF
Usage: $0 [options]

Install Mutineer BBS from a release tarball.

Options:
  --tarball PATH       Release tarball (default: ./mutineer.tar)
  --payload-dir PATH   Extracted mutineer payload directory instead of tarball
  --prefix PATH        Install root (default: \$HOME/mutineer)
  --config PATH        Config path (default: PREFIX/conf/mutineer.conf)
  --db PATH            SQLite DB path (default: PREFIX/data/mutineer.db)
  --data-dir PATH      Data directory (default: PREFIX/data)
  --logs-dir PATH      Logs directory (default: PREFIX/logs)
  --doors-dir PATH     Doors directory (default: PREFIX/doors)
  --runtime-dir PATH   Door runtime/dropfile directory (default: DATA_DIR/door_runtime)
  --user USER          Runtime user; used for ownership when run as root
  --group GROUP        Runtime group; defaults to USER
  --port PORT          BBS listener port override
  --no-install-deps    Report missing dependencies but do not install them
  --check              Verify installed layout without changing it
  -h, --help           Show this help
EOF
}

die() { echo "setup.sh: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"; }
abs_path() { realpath -m "$1"; }
take_arg() {
  [ "$#" -ge 2 ] || die "missing value for $1"
  printf '%s\n' "$2"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --tarball) TARBALL="$(take_arg "$@")"; shift 2 ;;
    --payload-dir) PAYLOAD_DIR="$(take_arg "$@")"; shift 2 ;;
    --prefix) PREFIX="$(take_arg "$@")"; shift 2 ;;
    --config) CONFIG="$(take_arg "$@")"; CONFIG_SET=1; shift 2 ;;
    --db) DB_PATH="$(take_arg "$@")"; DB_SET=1; shift 2 ;;
    --data-dir) DATA_DIR="$(take_arg "$@")"; DATA_SET=1; shift 2 ;;
    --logs-dir) LOGS_DIR="$(take_arg "$@")"; LOGS_SET=1; shift 2 ;;
    --doors-dir) DOORS_DIR="$(take_arg "$@")"; DOORS_SET=1; shift 2 ;;
    --runtime-dir) RUNTIME_DIR="$(take_arg "$@")"; RUNTIME_SET=1; shift 2 ;;
    --user) RUN_USER="$(take_arg "$@")"; shift 2 ;;
    --group) RUN_GROUP="$(take_arg "$@")"; shift 2 ;;
    --port) PORT="$(take_arg "$@")"; PORT_SET=1; shift 2 ;;
    --no-install-deps) INSTALL_DEPS=0; shift ;;
    --check) CHECK_ONLY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

install_runner() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  elif command -v doas >/dev/null 2>&1; then
    doas "$@"
  else
    return 127
  fi
}

detect_family() {
  os_id=""
  os_like=""
  if [ -r /etc/os-release ]; then
    . /etc/os-release
    os_id="${ID:-}"
    os_like="${ID_LIKE:-}"
  fi
  case " $os_id $os_like " in
    *" alpine "*) echo alpine ;;
    *" debian "*|*" ubuntu "*) echo debian ;;
    *" fedora "*|*" rhel "*|*" rocky "*|*" almalinux "*|*" centos "*) echo rhel ;;
    *" suse "*|*" opensuse "*) echo suse ;;
    *) echo unknown ;;
  esac
}

packages_for_family() {
  case "$1" in
    alpine)
      printf '%s\n' bash coreutils sqlite openssl gawk tar gzip libarchive-tools libdeflate libunistring ncurses-libs
      ;;
    debian)
      printf '%s\n' bash coreutils sqlite3 openssl gawk tar gzip libsqlite3-0
      first_available_package libarchive13t64 libarchive13
      printf '%s\n' libdeflate0
      first_available_package libunistring5 libunistring2
      printf '%s\n' libncursesw6 libtinfo6
      if apt-cache show libssl1.1 >/dev/null 2>&1; then
        printf '%s\n' libssl1.1
      else
        first_available_package libssl3t64 libssl3
      fi
      ;;
    rhel)
      printf '%s\n' bash coreutils sqlite openssl gawk tar gzip sqlite-libs openssl-libs libarchive libdeflate libunistring ncurses-libs
      ;;
    suse)
      printf '%s\n' bash coreutils sqlite3 openssl gawk tar gzip libsqlite3-0 libarchive13 libdeflate0 libunistring2 libncurses6
      if zypper --non-interactive search -x libopenssl1_1 >/dev/null 2>&1; then
        printf '%s\n' libopenssl1_1
      else
        printf '%s\n' libopenssl3
      fi
      ;;
    *)
      return 1
      ;;
  esac
}

first_available_package() {
  for package in "$@"; do
    if apt-cache show "$package" >/dev/null 2>&1; then
      printf '%s\n' "$package"
      return 0
    fi
  done
  die "none of the dependency packages are available: $*"
}

install_packages() {
  family="$1"
  shift
  [ "$#" -gt 0 ] || return 0
  echo "setup.sh: detected $family; missing dependency packages:"
  printf '  %s\n' "$@"
  if [ "$INSTALL_DEPS" -eq 0 ]; then
    die "missing dependencies and --no-install-deps was requested"
  fi
  case "$family" in
    alpine)
      install_runner apk add "$@" || die "unable to install dependencies with apk"
      ;;
    debian)
      install_runner apt-get update || die "unable to update apt package metadata"
      install_runner apt-get install -y "$@" || die "unable to install dependencies with apt-get"
      ;;
    rhel)
      if command -v dnf >/dev/null 2>&1; then
        install_runner dnf install -y "$@" || die "unable to install dependencies with dnf"
      else
        install_runner yum install -y "$@" || die "unable to install dependencies with yum"
      fi
      ;;
    suse)
      install_runner zypper --non-interactive install "$@" || die "unable to install dependencies with zypper"
      ;;
    *)
      die "unsupported Linux family; install dependencies manually"
      ;;
  esac
}

ensure_dependencies() {
  missing=()
  for cmd in realpath sqlite3 openssl awk install tar gzip sed grep mktemp; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
  done
  [ "${#missing[@]}" -eq 0 ] && return 0

  family="$(detect_family)"
  if [ "$family" = unknown ]; then
    echo "setup.sh: missing commands:"
    printf '  %s\n' "${missing[@]}"
    die "unsupported Linux distribution; install the missing commands manually"
  fi
  mapfile -t packages < <(packages_for_family "$family" | awk 'NF && !seen[$0]++')
  install_packages "$family" "${packages[@]}"
  for cmd in "${missing[@]}"; do
    command -v "$cmd" >/dev/null 2>&1 || die "required command still missing after dependency install: $cmd"
  done
}

ensure_dependencies

need realpath
need sqlite3
need openssl
need awk
need install
need tar
need gzip

PREFIX="$(abs_path "$PREFIX")"
TARBALL="$(abs_path "$TARBALL")"
[ -z "$PAYLOAD_DIR" ] || PAYLOAD_DIR="$(abs_path "$PAYLOAD_DIR")"
[ -n "$CONFIG" ] || CONFIG="$PREFIX/conf/mutineer.conf"
[ -n "$DATA_DIR" ] || DATA_DIR="$PREFIX/data"
[ -n "$LOGS_DIR" ] || LOGS_DIR="$PREFIX/logs"
[ -n "$DOORS_DIR" ] || DOORS_DIR="$PREFIX/doors"
[ -n "$DB_PATH" ] || DB_PATH="$DATA_DIR/mutineer.db"
[ -n "$RUNTIME_DIR" ] || RUNTIME_DIR="$DATA_DIR/door_runtime"
CONFIG="$(abs_path "$CONFIG")"
DATA_DIR="$(abs_path "$DATA_DIR")"
LOGS_DIR="$(abs_path "$LOGS_DIR")"
DOORS_DIR="$(abs_path "$DOORS_DIR")"
RUNTIME_DIR="$(abs_path "$RUNTIME_DIR")"
DB_PATH="$(abs_path "$DB_PATH")"
DROPFILE_DIR="$RUNTIME_DIR/dropfiles"
[ -z "$RUN_GROUP" ] || true
[ -n "$RUN_GROUP" ] || RUN_GROUP="$RUN_USER"

if [ -n "$PORT" ]; then
  case "$PORT" in *[!0-9]*|'') die "invalid port: $PORT" ;; esac
fi

if [ -n "$RUN_USER" ]; then
  getent passwd "$RUN_USER" >/dev/null || die "runtime user does not exist: $RUN_USER"
fi
if [ -n "$RUN_GROUP" ]; then
  getent group "$RUN_GROUP" >/dev/null || die "runtime group does not exist: $RUN_GROUP"
fi

TMP_PAYLOAD=""
cleanup() {
  [ -z "$TMP_PAYLOAD" ] || rm -rf "$TMP_PAYLOAD"
}
trap cleanup EXIT

prepare_payload() {
  if [ -n "$PAYLOAD_DIR" ]; then
    PAYLOAD="$PAYLOAD_DIR"
    return
  fi
  [ -f "$TARBALL" ] || die "release tarball is missing: $TARBALL"
  TMP_PAYLOAD="$(mktemp -d)"
  tar -C "$TMP_PAYLOAD" -xf "$TARBALL"
  PAYLOAD="$TMP_PAYLOAD/mutineer"
}

PAYLOAD=""
prepare_payload

required_bins=(
  mutineer
  bin/mutineer-console
  bin/mutineer-initbbs
  bin/mutineer-qwkgen
  bin/mutineer-msgpack
  bin/mutineer-userpack
  bin/mutineer-filepack
  bin/mutineer-stats
  bin/mutineer-maint
  bin/mutineer-validate
  bin/mutineer-netmail-export
  bin/mutineer-rest
)
required_plank_bins=(
  plank/bin/plankd
  plank/bin/coved
  plank/bin/plankctl
  plank/bin/plankpack
  plank/bin/plank-offline
)
required_plugins=(
  plugins/chat_plugin.so
  plugins/hello.so
)
required_libs=(
  lib/libnotcurses-core.so
  lib/libtinfo.so.6
  lib/libunistring.so.2
  lib/libdeflate.so.0
  lib/libssl.so.1.1
  lib/libcrypto.so.1.1
)
required_docs=(LICENSE README.md INSTALL.md VERSION MANIFEST docs/LICENSE)

check_payload() {
  [ -d "$PAYLOAD" ] || die "payload directory does not exist: $PAYLOAD"
  for path in "${required_bins[@]}" "${required_plank_bins[@]}" "${required_plugins[@]}" "${required_libs[@]}" "${required_docs[@]}" conf/mutineer.conf sql/schema.sql; do
    [ -e "$PAYLOAD/$path" ] || die "payload missing required file: $path"
  done
  [ -x "$PAYLOAD/buccaneer/bin/bucc" ] || die "payload missing required file: buccaneer/bin/bucc"
  [ -d "$PAYLOAD/buccaneer/include/buccaneer" ] || die "payload missing Buccaneer headers"
  [ -d "$PAYLOAD/buccaneer/examples" ] || die "payload missing Buccaneer examples"
}

check_installed() {
  for path in "${required_bins[@]}" "${required_plank_bins[@]}" "${required_plugins[@]}" "${required_libs[@]}" LICENSE README.md INSTALL.md VERSION MANIFEST docs/LICENSE conf/mutineer.conf sql/schema.sql; do
    [ -e "$PREFIX/$path" ] || die "installed tree missing required file: $PREFIX/$path"
  done
  [ -x "$PREFIX/buccaneer/bin/bucc" ] || die "installed tree missing required file: $PREFIX/buccaneer/bin/bucc"
  [ -d "$PREFIX/buccaneer/include/buccaneer" ] || die "installed tree missing Buccaneer headers"
  [ -d "$PREFIX/buccaneer/examples" ] || die "installed tree missing Buccaneer examples"
  [ -d "$DATA_DIR" ] || die "data directory is missing: $DATA_DIR"
  [ -d "$LOGS_DIR" ] || die "logs directory is missing: $LOGS_DIR"
  [ -d "$DOORS_DIR" ] || die "doors directory is missing: $DOORS_DIR"
  [ -d "$RUNTIME_DIR" ] || die "runtime directory is missing: $RUNTIME_DIR"
  [ -d "$DROPFILE_DIR" ] || die "dropfile directory is missing: $DROPFILE_DIR"
  ldd "$PREFIX/mutineer" >/dev/null || die "mutineer shared-library check failed"
  echo "Mutineer installation check passed."
}

if [ "$CHECK_ONLY" -eq 1 ]; then
  check_installed
  exit 0
fi

check_payload

install_tree() {
  config_created=0
  mkdir -p "$PREFIX" "$DATA_DIR" "$LOGS_DIR" "$DOORS_DIR" "$RUNTIME_DIR" "$DROPFILE_DIR"
  mkdir -p "$PREFIX/bin" "$PREFIX/plank/bin" "$PREFIX/plugins" "$PREFIX/lib" "$PREFIX/conf" \
    "$PREFIX/art" "$PREFIX/menus" "$PREFIX/sql" "$PREFIX/scripts" "$PREFIX/docs" \
    "$PREFIX/buccaneer"

  install -m 0755 "$PAYLOAD/mutineer" "$PREFIX/mutineer"
  cp -a "$PAYLOAD/bin/." "$PREFIX/bin/"
  [ ! -d "$PAYLOAD/plank" ] || cp -a "$PAYLOAD/plank" "$PREFIX/"
  [ ! -d "$PAYLOAD/buccaneer" ] || cp -a "$PAYLOAD/buccaneer" "$PREFIX/"
  [ ! -d "$PAYLOAD/plugins" ] || cp -a "$PAYLOAD/plugins/." "$PREFIX/plugins/"
  [ ! -d "$PAYLOAD/lib" ] || cp -a "$PAYLOAD/lib/." "$PREFIX/lib/"
  cp -a "$PAYLOAD/art/." "$PREFIX/art/"
  cp -a "$PAYLOAD/menus/." "$PREFIX/menus/"
  cp -a "$PAYLOAD/sql/." "$PREFIX/sql/"
  cp -a "$PAYLOAD/scripts/." "$PREFIX/scripts/" 2>/dev/null || true
  cp -a "$PAYLOAD/doors/." "$DOORS_DIR/" 2>/dev/null || true
  cp -a "$PAYLOAD/docs/." "$PREFIX/docs/"
  install -m 0644 "$PAYLOAD/LICENSE" "$PREFIX/LICENSE"
  install -m 0644 "$PAYLOAD/LICENSE" "$PREFIX/docs/LICENSE"
  install -m 0644 "$PAYLOAD/README.md" "$PREFIX/README.md"
  install -m 0644 "$PAYLOAD/INSTALL.md" "$PREFIX/INSTALL.md"
  install -m 0644 "$PAYLOAD/VERSION" "$PREFIX/VERSION"
  install -m 0644 "$PAYLOAD/MANIFEST" "$PREFIX/MANIFEST"

  if [ ! -f "$CONFIG" ]; then
    install -m 0644 "$PAYLOAD/conf/mutineer.conf" "$CONFIG"
    config_created=1
  fi
}

set_config_value() {
  key="$1"
  value="$2"
  tmp="$CONFIG.tmp.$$"
  awk -v key="$key" -v value="$value" '
    BEGIN { done = 0 }
    index($0, key "=") == 1 {
      print key "=" value
      done = 1
      next
    }
    { print }
    END {
      if (!done)
        print key "=" value
    }
  ' "$CONFIG" >"$tmp"
  mv "$tmp" "$CONFIG"
}

configure_installation() {
  [ -f "$CONFIG" ] || die "config file was not installed: $CONFIG"

  if [ "$config_created" -eq 1 ] || [ "$PORT_SET" -eq 1 ]; then
    [ -n "$PORT" ] && set_config_value port "$PORT"
  fi
  if [ "$config_created" -eq 1 ] || [ "$DB_SET" -eq 1 ]; then
    set_config_value db_path "$DB_PATH"
  fi
  if [ "$config_created" -eq 1 ] || [ "$DATA_SET" -eq 1 ]; then
    set_config_value data_path "$DATA_DIR"
  fi
  if [ "$config_created" -eq 1 ] || [ "$LOGS_SET" -eq 1 ]; then
    set_config_value logs_path "$LOGS_DIR/mutineer.log"
  fi
  if [ "$config_created" -eq 1 ] || [ "$DOORS_SET" -eq 1 ]; then
    set_config_value doors_path "$DOORS_DIR"
  fi
  if [ "$config_created" -eq 1 ] || [ "$RUNTIME_SET" -eq 1 ]; then
    set_config_value dropfile_path "$DROPFILE_DIR"
    set_config_value door_runtime_path "$RUNTIME_DIR"
  fi
  if [ "$config_created" -eq 1 ]; then
    set_config_value art_path "$PREFIX/art"
    set_config_value menu_main "$PREFIX/menus/main.mnu"
    set_config_value plugins_dir "$PREFIX/plugins"
    set_config_value protocol_path "$PREFIX/conf/protocols.conf"
  fi
}

chown_if_requested() {
  [ -n "$RUN_USER" ] || return 0
  owner="$RUN_USER"
  [ -z "$RUN_GROUP" ] || owner="$owner:$RUN_GROUP"
  if [ "$(id -u)" -eq 0 ]; then
    chown -R "$owner" "$PREFIX"
  else
    echo "setup.sh: not root; leaving ownership unchanged for $PREFIX" >&2
  fi
}

install_tree
configure_installation

if [ ! -f "$DB_PATH" ]; then
  (cd "$PREFIX" && ./bin/mutineer-initbbs -c "$CONFIG" -y)
fi

chown_if_requested
check_installed

cat <<EOF

Setup complete.

Install root:
  $PREFIX

Config:
  $CONFIG

Database:
  $DB_PATH

Start Mutineer with:
  cd $PREFIX && ./mutineer -c $CONFIG
EOF
