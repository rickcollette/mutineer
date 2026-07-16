# Mutineer Packaging Workspace

This directory contains the proposed self-contained packaging system for
Mutineer proper. It is intentionally not wired into the root `Makefile` yet.

Files:

- `Dockerfile.dist`: Debian 10/Buster build image for glibc 2.28-compatible
  release binaries.
- `Makefile.fragment`: proposed root Makefile targets.
- `package.sh`: stages `dist/mutineer/`, creates `dist/mutineer.tar`, and
  creates the extensionless self-extracting `dist/mutineer-install`.
- `make-self-extracting-installer.sh`: combines `setup.sh` and the tar payload.
- `setup.sh`: target-host installer with dependency detection.

Dry-run from the Mutineer repo root after review:

```sh
make -f packaging/Makefile.fragment dist
```

Expected artifacts:

```text
dist/mutineer.tar
dist/mutineer-install
```

The staged `dist/mutineer/` payload is intended to cover the current
`dist/mutineer*` release contents plus the existing standalone
`dist/buccaneer/` contents under `mutineer/buccaneer/`.

The generated installer is extensionless by design.
