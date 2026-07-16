# Anavir Runbook

## Platform and startup

Linux/POSIX is the only supported server platform. Build and start from the repository root:

```sh
make -C src clean && make -C src
src/mutineermud 4000
```

For an installed service, set the canonical database explicitly:

```sh
ANAVIR_DB_PATH=/var/lib/anavir/anavir.db /opt/anavir/mutineermud 4000
```

The fallback is `data/anavir.db` resolved from the executable/repository layout. No `area/` working directory is required.

## Distribution and installation

Build the deployment package on a host with Docker available:

```sh
make dist
```

`make dist` builds inside a Debian 10/Buster container so the release binaries
are linked against glibc 2.28.x for backward compatibility with older Linux
hosts. It writes:

- `dist/mutineermud.tar`: portable payload containing `bin/mutineermud`,
  `bin/anavir-door-client`, `data/anavir.db`, `docs/MANUAL.md`,
  `docs/LICENSE.md`, `docs/NOTICE.md`, `docs/COPYING`,
  `docs/COPYING.LESSER`, `docs/SOURCE_HEADERS.md`, and `MANIFEST`.
- `dist/mutineermud-install`: preferred self-extracting installer containing
  the same payload plus `setup.sh`.

On the Mutineer BBS host, copy only `dist/mutineermud-install`, make it
executable, and run it as the BBS user:

```sh
chmod 755 mutineermud-install
./mutineermud-install --mutineer-root /path/to/mutineer
./mutineermud-install --mutineer-root /path/to/mutineer --check
```

The installer checks for required host commands before making changes. On
Rocky Linux, AlmaLinux, RHEL, Fedora, Debian, Ubuntu, Alpine Linux, and SUSE or
openSUSE, it prints the dependency package list and uses the local package
manager (`dnf`, `yum`, `apt-get`, `apk`, or `zypper`) through root, `sudo`, or
`doas` when dependencies are missing. Use `--no-install-deps` to report missing
dependencies without installing them.

If the installer is run beside a standard source checkout, `--mutineer-root`
defaults to `../../mutineer`; production installs should pass the path
explicitly. The script installs Anavir under `MUTINEER_ROOT/doors/anavir`,
copies mutable game state to `MUTINEER_ROOT/data/anavir/anavir.db`, creates
`MUTINEER_ROOT/data/run/anavir.sock` by default, registers the Mutineer native
door record, enables the leaderboard, configures the door janitor, and writes a
private `anavir-mutineer.env`. The packaged manual is installed to
`MUTINEER_ROOT/docs/mutineermud/MANUAL.md`; project license terms, DikuMUD
attribution, GPL/LGPL texts, and source-header guidance are installed beside it.

Common overrides:

```sh
./mutineermud-install \
  --mutineer-root /srv/mutineer \
  --mutineer-config /srv/mutineer/conf/mutineer.conf \
  --mutineer-db /srv/mutineer/data/mutineer.db \
  --install-dir /srv/mutineer/doors/anavir \
  --state-dir /srv/mutineer/data/anavir \
  --run-dir /srv/mutineer/data/run \
  --anavir-port 4000 \
  --acs L20 \
  --daemon-user bbs
```

`--daemon-user` and `--daemon-group` validate the requested account. Ownership
is changed only when the installer is run as root; when run as the BBS user, the
installed files are already owned by that user.

The split-artifact flow is still supported for hosts that cannot use the
self-extracting file. Copy `tools/setup.sh` as `setup.sh`, copy
`dist/mutineermud.tar` to the same host directory, and run:

```sh
chmod 755 setup.sh
./setup.sh --tarball ./mutineermud.tar --mutineer-root /path/to/mutineer
```

For development only, `make dist-local` builds the package on the current host
and `tools/setup.sh --build-source` builds and installs from the current source
checkout instead of a tarball. Production deployment should use `make dist`
output so the host install is reproducible and glibc-compatible.

After installation, start Anavir with the generated runner and restart Mutineer
so it reloads the door record:

```sh
/path/to/mutineer/doors/anavir/run-anavir.sh
```

Re-run the installer whenever a new Anavir package is deployed. It is
idempotent and preserves the existing Mutineer secret unless the config still
uses the development default.

## Licenses

MutineerMUD is distributed with the project license in root `LICENSE.md`.
DikuMUD attribution and the upstream DikuMUD licensing notice are documented in
root `NOTICE.md`. The full GPL and LGPL texts are included as root `COPYING`
and `COPYING.LESSER`.

Installed systems receive these files under
`MUTINEER_ROOT/docs/mutineermud/`. Keep them with redistributed binaries and
review them before publishing modified packages.

## Inspection, backup, and migration

```sh
sqlite3 data/anavir.db 'select key,value from schema_meta order by key;'
sqlite3 data/anavir.db 'pragma foreign_key_check;'
sqlite3 data/anavir.db '.backup /safe/anavir-$(date +%F).db'
python3 tools/import_legacy_runtime_state.py --db data/anavir.db
```

Import runs once and refuses changed/already-imported sources unless an operator reviews them and supplies `--replace`. A legacy non-SHA512 password also requires the verified current password through `--legacy-password`; plaintext compatibility is not supported. Always back up first.

## Export and validation

```sh
python3 tools/export_anavir_db.py
python3 tools/validate_anavir_db.py
python3 tools/scan_runtime_dependencies.py --self-test
python3 tools/scan_runtime_dependencies.py
```

`exports/anavir_snapshot.txt` is review-only. Validation compares its schema version, content epoch, and logical database fingerprint and fails if stale.

## Acceptance

```sh
tools/acceptance_gate.sh
```

The gate is bounded to 900 seconds. Gameplay/IPC/BBS phases are bounded to 210 seconds and the combined runtime/reroll/OLC/death phase to 360 seconds. It runs native color/free-list regressions, prints progress, captures transcript/log tails, terminates process groups, uses temporary database copies and collision-resistant ports, and cleans sockets, databases, objects, binaries, and Python caches. A nonzero result means the project is not complete.

The only approved external drop-files are `DOOR32.SYS` and `MUTINEER_SESSION.JSON`, read by `anavir-door-client`. Session expiry, HMAC, nonce replay, malformed/missing input, and path handling are covered by the IPC and door suites.

## Mutineer leaderboard integration

The bundled setup selects Mutineer's built-in salted PBKDF2-SHA256 password
backend (`password_upgrade=0`). Argon2 is opt-in upstream and is not required
for this BBS deployment, so normal configuration does not probe for it.

The installer creates a private shared-secret environment, configures the Unix
socket and janitor settings, migrates older Mutineer door schemas, registers the
native installed Anavir command, enables the canonical leaderboard, and checks
both databases. Use `--help` for installed paths, alternate databases, ports,
ACS expressions, door names, leaderboard labels, or daemon ownership.

Configure the Anavir door in Mutineer's `doors` table with `lb_enable=1`, a
stable `lb_key` (recommended: `anavir`), `lb_label='Power'`, and
`lb_order='desc'`. On an explicit in-game `quit`, the server computes the
authenticated character's power score as `level * 1,000,000 + experience`.
The door client strips the private server trailer from terminal output and
writes `MUTINEER_LB_RESULT.JSON` in its launch directory. Mutineer attributes
and ingests that result using the authenticated BBS handle; Anavir does not
maintain a parallel leaderboard database or public endpoint.

Mutineer's door janitor owns stale launch cleanup. It preserves connected
nodes, removes aged native/DOS launch trees after a node disconnects, and
reconciles orphaned `nodes.status='online'` rows. Anavir itself never traverses
or deletes Mutineer's runtime directories.
