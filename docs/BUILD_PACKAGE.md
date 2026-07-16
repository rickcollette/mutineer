# Mutineer Build and Packaging Plan

This document describes how Mutineer proper should adopt the same release
packaging shape used by the MutineerMUD door package, adjusted for Mutineer's
existing CMake build, release script, tools, docs, and MIT license.

## Goals

- `make dist` must build release binaries inside a Docker container based on
  Debian 10/Buster so the shipped Linux binaries are compatible with glibc
  2.28.x systems.
- The release artifact must be installable from one extensionless
  self-extracting file, not a `.sh` file.
- The installer must be idempotent, path-configurable, dependency-aware, and
  usable on common Linux distributions.
- Root should remain a source/release root, not a scratch area. Generated
  packages, temporary build products, PID files, logs, and runtime databases
  must stay out of the committed tree.
- Mutineer only has the root `LICENSE` file, which is MIT. That file must be
  packaged and installed with the documentation.
- Mutineer's pertinent documentation lives under `docs/`; package docs should
  be sourced from there and installed under the target docs tree.

## Current State

The root `Makefile` is a convenience wrapper around CMake:

- `make` runs `version-bump`, configures `build-make`, and builds `mutineer`.
- `make test` builds all CMake targets and runs `ctest`.
- `make dist` currently runs `dist-mutineer` and `dist-buccaneer`.
- `dist-mutineer` builds `all tools plank plugins`, then invokes
  `scripts/build-release.sh`.
- `scripts/build-release.sh` currently stages platform-specific tarballs such
  as `mutineer-VERSION-x86_64-debian.tar.gz`.
- It ships:
  - root `mutineer`
  - maintenance tools under `bin/`
  - PLANK tools under `plank/bin/`
  - plugins under `plugins/`
  - `conf/`, `art/`, `menus/`, `sql/`, selected `scripts/`
  - root `README.md` and `LICENSE`
  - optional sample `doors/testdoor`
  - generated `INSTALL.md` and `VERSION`
- The existing `dist-mutineer` also copies the latest Debian package to
  `dist/mutineer` for legacy `bbs-up`/`scripts/start` behavior.

The new system should reuse the useful staging knowledge in
`scripts/build-release.sh`, but change the release entrypoint and final
artifact shape.

## Target Developer Commands

Use these public targets:

```sh
make
make test
make dist
make dist-local
make dist-clean
```

`make` remains a local developer build.

`make dist` becomes the official release build and must always run inside the
Docker compatibility image.

`make dist-local` is an internal/developer target. It performs the staging and
packaging work on the current host. `make dist` calls it from inside Docker.

`make dist-clean` removes generated package artifacts.

## Docker Compatibility Build

Add:

```text
tools/Dockerfile.dist
```

Use Debian 10/Buster:

```dockerfile
FROM debian:buster
```

Because Buster is archived, configure archive repositories and disable
`Check-Valid-Until`. Install the build dependencies required by the existing
CMake project:

- `build-essential`
- `cmake`
- `make`
- `pkg-config`
- `sqlite3`
- `libsqlite3-dev`
- `libssl-dev`
- `libarchive-dev`
- `libnotcurses-dev` if the current build still requires it
- `libc6-dev`
- `bash`
- `coreutils`
- `tar`
- `gzip`
- `ca-certificates`

Keep Argon2 optional. Do not make `libargon2-dev` part of the required
compatibility build unless `MUTINEER_ENABLE_ARGON2=ON` is explicitly selected.

The image should print `ldd --version | head -1` during build so the logs prove
the builder is glibc 2.28.

Suggested Makefile pattern:

```make
DIST_IMAGE ?= mutineer-dist-glibc228

.PHONY: docker-image dist dist-local dist-clean

docker-image:
	docker build -f tools/Dockerfile.dist -t "$(DIST_IMAGE)" .

dist: docker-image
	docker run --rm \
	  -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/work" \
	  -w /work \
	  "$(DIST_IMAGE)" \
	  make dist-local
```

`dist-local` must clean and rebuild inside the container. It must not reuse
host-built binaries:

```make
dist-local:
	rm -rf "$(BUILD_DIR)"
	$(MAKE) CMAKE_BUILD_TYPE=Release all tools plank plugins
	# stage package, create tar, create self-extracting installer
```

Alternatively, call CMake directly in `dist-local` if that is cleaner than
recursing through existing Make targets. The important rule is that the release
binaries are built after the Docker container starts.

## Release Artifact Names

Produce:

```text
dist/mutineer.tar
dist/mutineer-install
```

Do not name the self-extracting installer `*.sh`.

The extensionless installer is the only file an operator should need for a
normal install. The tarball remains useful for debugging, mirrors, and
split-artifact installs.

## Staged Payload Layout

Stage this tree before creating `dist/mutineer.tar`:

```text
dist/mutineer/
  MANIFEST
  VERSION
  INSTALL.md
  LICENSE
  README.md
  mutineer
  bin/
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
  plank/bin/
    plankd
    coved
    plankctl
    plankpack
    plank-offline
  plugins/
    *.so
  conf/
  art/
  menus/
  sql/
  scripts/
  doors/
  docs/
    *.md
    reference/
    buccaneer/
```

Include the root MIT `LICENSE` both at package root and in the installed docs
area. Do not invent extra license files for Mutineer unless the project later
adds them.

The staged `docs/` directory should come from the source `docs/` tree. It
should include the new `docs/BUILD_PACKAGE.md` so the package describes how it
was produced.

Do not stage build directories, tests, `.git`, `dist/`, runtime logs, live
databases, generated door installs, or package caches.

## Manifest

Create `dist/mutineer/MANIFEST` with at least:

```text
name=mutineer
version=<VERSION or dev>
platform=linux-x86_64-glibc228
built_at=<UTC ISO timestamp>
commit=<GITHUB_SHA or local>
build_glibc=<ldd --version first line>
```

If possible, include SHA-256 hashes for the primary binaries:

```text
sha256.mutineer=<hash>
sha256.bin.mutineer-initbbs=<hash>
sha256.bin.mutineer-validate=<hash>
```

After staging, enforce that shipped binaries do not require GLIBC newer than
2.28:

```sh
strings dist/mutineer/mutineer dist/mutineer/bin/* dist/mutineer/plank/bin/* |
  grep -Eo 'GLIBC_[0-9]+\.[0-9]+' |
  sort -Vu |
  awk -F_ 'BEGIN{bad=0}
           {split($2,v,"."); if (v[1] > 2 || (v[1] == 2 && v[2] > 28)) bad=1}
           END{exit bad}'
```

Fail the package if that check fails.

## Self-Extracting Installer

Add or reuse:

```text
tools/make_self_extracting_installer.sh
tools/setup.sh
```

`tools/make_self_extracting_installer.sh` should:

- accept `SETUP_SH`, `TARBALL`, `OUTPUT`
- write an executable extensionless shell archive
- embed `tools/setup.sh`
- append `dist/mutineer.tar` as base64
- extract both to a temporary directory at runtime
- execute:

```sh
setup.sh --tarball /tmp/.../mutineer.tar "$@"
```

The generated file must be:

```text
dist/mutineer-install
```

## Installer Behavior

`tools/setup.sh` should install Mutineer from the tarball. It should not assume
it is running from a source checkout.

Recommended options:

```text
--tarball PATH
--payload-dir PATH
--prefix PATH
--config PATH
--db PATH
--data-dir PATH
--logs-dir PATH
--doors-dir PATH
--runtime-dir PATH
--user USER
--group GROUP
--port PORT
--no-install-deps
--check
-h, --help
```

Suggested defaults:

```text
--prefix        $HOME/mutineer
--config        PREFIX/conf/mutineer.conf
--db            PREFIX/data/mutineer.db
--data-dir      PREFIX/data
--logs-dir      PREFIX/logs
--doors-dir     PREFIX/doors
--runtime-dir   PREFIX/run
--port          value already present in config, or Mutineer's current default
```

The installer should:

- install binaries into `PREFIX/` and `PREFIX/bin/`
- install PLANK tools into `PREFIX/plank/bin/`
- install docs into `PREFIX/docs/`
- install MIT `LICENSE` into `PREFIX/docs/LICENSE` and/or package root
- install `conf/`, `art/`, `menus/`, `sql/`, selected runtime `scripts/`
- create `data/`, `logs/`, `doors/`, and runtime directories
- preserve an existing `conf/mutineer.conf` unless an explicit replace option
  is added
- initialize `data/mutineer.db` with `mutineer-initbbs` only when the DB is
  absent
- apply any schema migrations through the normal Mutineer tool path
- write or update path settings in config only when requested or when creating
  a new config
- validate the installed tree in `--check`

`--check` should verify:

- main daemon exists and is executable
- all required CLI tools exist and are executable
- PLANK tools exist and are executable if packaged
- config exists
- SQLite DB exists, or init tool is available to create it
- docs and `LICENSE` exist
- `sql/schema.sql` exists
- required directories exist
- `mutineer-validate` passes, if it supports validating the installed config
- shared-library dependencies resolve with `ldd`

## Dependency Bootstrap

Before making changes, `tools/setup.sh` should detect missing host
dependencies and offer to install them.

Supported Linux families:

- Debian and Ubuntu via `apt-get`
- Rocky Linux, AlmaLinux, RHEL, Fedora, and CentOS-like systems via `dnf` or
  `yum`
- Alpine Linux via `apk`
- SUSE and openSUSE via `zypper`

The installer should read `/etc/os-release` and use `ID` plus `ID_LIKE`.

It should print the package list before installing. Use root directly, or
`sudo`/`doas` when available. If no privilege escalation tool exists, fail with
the package list.

Minimum runtime command dependencies:

- `bash`
- `coreutils` / `realpath`
- `sqlite3`
- `openssl`
- `awk`
- `tar`
- `gzip`
- `install`
- `sed`
- `grep`
- `mktemp`

Runtime library package names should be mapped per family. Start with:

| Family | Packages |
| --- | --- |
| Debian/Ubuntu | `bash coreutils sqlite3 openssl gawk tar gzip libsqlite3-0 libssl1.1 libarchive13` for Buster-compatible systems; document that newer distributions may provide `libssl3` instead |
| Rocky/Alma/RHEL/Fedora | `bash coreutils sqlite openssl gawk tar gzip sqlite-libs openssl-libs libarchive` |
| Alpine | `bash coreutils sqlite openssl gawk tar gzip sqlite-libs libcrypto3 libarchive-tools` |
| SUSE/openSUSE | `bash coreutils sqlite3 openssl gawk tar gzip libsqlite3-0 libopenssl1_1 libarchive13` or distribution equivalents |

If the installer supports `--build-source`, also install build dependencies:

- C compiler and make
- CMake
- pkg-config
- SQLite development headers
- OpenSSL development headers
- libarchive development headers
- notcurses development headers if still required
- Python only if a Mutineer setup step needs it

Support:

```text
--no-install-deps
```

This should print what is missing and fail without installing.

## Relationship To Existing `scripts/build-release.sh`

There are two reasonable implementation paths:

1. Refactor `scripts/build-release.sh` into the staging engine for
   `dist-local`.
2. Keep `scripts/build-release.sh` for old platform tarballs and create a new
   `scripts/build-package.sh` for the Docker/self-extracting flow.

Prefer option 2 if it keeps risk lower. The current release script is already
platform-specific (`debian`, `fedora`, `alpine`) and emits `.tar.gz` archives;
the new system emits one glibc-2.28-compatible Linux package plus an
extensionless installer.

The old `release-debian`, `release-fedora`, and `release-alpine` targets can
remain temporarily, but `make dist` should become the new official path.

## Interaction With Buccaneer Packaging

Current `make dist` also builds `dist-buccaneer`.

Decide whether Mutineer's official package should include Buccaneer as part of
the BBS release, or whether Buccaneer remains separately packaged. Recommended
approach:

- `make dist` builds the Mutineer BBS package only.
- `make dist-buccaneer` remains available for the standalone Buccaneer
  toolchain.
- Add `make dist-all` if both artifacts should be produced together.

This avoids surprising BBS operators with multiple unrelated artifacts.

## Documentation Updates

Update these docs after implementation:

- `README.md`: top-level package/install quick path.
- `docs/deployment.md`: operator install and upgrade procedure.
- `docs/getting-started.md`: point new installs at `dist/mutineer-install`.
- `docs/sysop-guide.md`: installed layout and service management.
- `docs/BUILD_PACKAGE.md`: keep this file current with the implemented target
  names and installer options.

Do not create root `RUNBOOK.md`, `TODO.md`, or `PLAN.md`.

## Verification Checklist

After implementation, Codex should run:

```sh
make clean
make dist
tar -tf dist/mutineer.tar
strings dist/mutineer/mutineer dist/mutineer/bin/* dist/mutineer/plank/bin/* |
  grep -Eo 'GLIBC_[0-9]+\.[0-9]+' |
  sort -Vu
```

The GLIBC list must not contain anything newer than `GLIBC_2.28`.

Then test install into a temporary prefix:

```sh
tmp=$(mktemp -d)
install -m 0755 dist/mutineer-install "$tmp/mutineer-install"
"$tmp/mutineer-install" --prefix "$tmp/root"
"$tmp/mutineer-install" --prefix "$tmp/root" --check
```

Verify:

- no generated files remain outside `dist/` after `make dist-clean`
- no object files or built binaries remain in source directories after
  `make clean`
- installed config and database are usable
- `mutineer-initbbs` can initialize a fresh DB
- `mutineer-validate` passes against the installed config, if applicable
- `LICENSE` is installed with docs

## Final Shape

The desired operator flow is:

```sh
make dist
scp dist/mutineer-install bbs-host:/tmp/
ssh bbs-host
chmod 755 /tmp/mutineer-install
/tmp/mutineer-install --prefix /srv/mutineer
/tmp/mutineer-install --prefix /srv/mutineer --check
```

The desired source-root shape remains:

```text
.gitignore
CMakeLists.txt
LICENSE
Makefile
README.md
art/
conf/
docs/
doors/
include/
menus/
plugins/
scripts/
sql/
src/
tests/
tools/
```

Generated output belongs in `build-*` and `dist/`, both removable.
