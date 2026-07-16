# Mutineer Packaging Workspace

This directory contains Mutineer's canonical self-contained packaging system.
It is wired into the root `Makefile`; `make dist` always builds inside the
pinned Docker image.

Files:

- `Dockerfile.dist`: Debian 10/Buster build image for glibc 2.28-compatible
  release binaries. It installs a modern CMake as a build tool because Buster's
  packaged CMake is too old for Mutineer, and builds notcurses-core from source
  for `mutineer-console`.
- `Makefile.fragment`: Docker distribution targets included by the root
  `Makefile`.
- `package.sh`: stages `dist/mutineer/`, creates `dist/mutineer.tar`, and
  creates the executable, extensionless self-extracting shell archive
  `dist/mutineer-install`.
- `make-self-extracting-installer.sh`: combines `setup.sh` and the tar payload.
- `setup.sh`: target-host installer with dependency detection.

Build from the Mutineer repository root:

```sh
make dist
```

Expected artifacts:

```text
dist/mutineer.tar
dist/mutineer-install
```

The staged `dist/mutineer/` payload is intended to cover the current
`dist/mutineer*` release contents plus the existing standalone
`dist/buccaneer/` contents under `mutineer/buccaneer/`.

`mutineer-console` depends on notcurses-core. The glibc-2.28 build image builds
that library from source. `package.sh` bundles `libnotcurses-core.so*` and the
OpenSSL 1.1 runtime selected by the compatibility build under
`dist/mutineer/lib/`; release binaries search both `$ORIGIN/lib` and
`$ORIGIN/../lib`, covering executables at the package root and under `bin/`.

The generated installer is extensionless by design. It is a shell archive with
an executable shebang, embedded setup program, and base64-encoded tar payload.
It can be copied to another compatible Linux host and run directly:

```sh
chmod 755 dist/mutineer-install
dist/mutineer-install --prefix /srv/mutineer
```

`make dist-local` exists for development and for invocation from inside the
container. It is not the release entry point because it builds against the
current host.

Notes:

- The Docker image intentionally uses Debian archive repositories so release
  binaries are built against glibc 2.28. Those archives can be slow; the
  Dockerfile configures apt retries and timeouts, but a first build may still
  take several minutes.
- CMake and notcurses versions are build arguments:

```sh
docker build \
  --build-arg CMAKE_VERSION=3.28.6 \
  --build-arg NOTCURSES_VERSION=3.0.13 \
  -f packaging/Dockerfile.dist \
  -t mutineer-dist-glibc228 .
```
