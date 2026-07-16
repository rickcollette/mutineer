#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: $0 SETUP_SH TARBALL OUTPUT" >&2
  exit 2
fi

setup_sh="$1"
tarball="$2"
output="$3"

[ -f "$setup_sh" ] || { echo "missing setup script: $setup_sh" >&2; exit 1; }
[ -f "$tarball" ] || { echo "missing tarball: $tarball" >&2; exit 1; }

tmp="$(mktemp "${output}.XXXXXX")"
trap 'rm -f "$tmp"' EXIT

cat >"$tmp" <<'SHAR_HEADER'
#!/usr/bin/env bash
set -euo pipefail

tmpdir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT

script="$0"
awk '
  $0 == "__MUTINEER_SETUP_SH__" { in_setup=1; next }
  $0 == "__MUTINEER_TARBALL_B64__" { in_setup=0; in_tar=1; next }
  in_setup { print > setup_path; next }
  in_tar { print > tar_path; next }
' setup_path="$tmpdir/setup.sh" tar_path="$tmpdir/mutineer.tar.b64" "$script"

chmod 0755 "$tmpdir/setup.sh"
base64 -d "$tmpdir/mutineer.tar.b64" >"$tmpdir/mutineer.tar"
exec "$tmpdir/setup.sh" --tarball "$tmpdir/mutineer.tar" "$@"

__MUTINEER_SETUP_SH__
SHAR_HEADER

cat "$setup_sh" >>"$tmp"
cat >>"$tmp" <<'SHAR_MIDDLE'

__MUTINEER_TARBALL_B64__
SHAR_MIDDLE
base64 "$tarball" >>"$tmp"
chmod 0755 "$tmp"
mv "$tmp" "$output"
trap - EXIT
