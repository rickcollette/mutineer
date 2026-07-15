# Mutineer Completion Checklist

Review date: 2026-07-15

Scope: end-to-end source, tests, docs, scripts, Docker, PLANK/COVE,
Buccaneer, and the new standalone console split. This list tracks work still
needed to make the project internally consistent and production-ready. Checked
items are foundations already verified during this review.

## P0 - Must Finish

- [x] Finish `mutineer-console` passthrough as true full-duplex terminal I/O.
  - Evidence: `src/tools/mutineer-console.c` currently enters passthrough by
    reading server lines and printing them, but does not forward local keyboard
    input back to the server.
  - Required result: `menu.session.start` can actually drive `useredit`,
    `eventeditor`, `validatefiles`, message read/write, local-logon-style flows,
    and server-side shell sessions from the console.

- [x] Add console passthrough regression coverage.
  - Verified: `tests/test_console_protocol.sh` starts a disposable daemon,
    invokes `menu.session.start` with the existing `who` action, enters
    passthrough, sends the required response keystroke, and verifies
    `passthrough.end`.

- [x] Apply existing login throttling/audit patterns to console authentication.
  - Verified: console authentication uses shared login throttle helpers and
    audits success, bad password, non-sysop denial, and throttled attempts.

- [x] Harden console node command validation.
  - Evidence: `node.lock`, `node.unlock`, `node.inspect`, and `node.kick` accept
    parsed node integers without a shared bounds/error policy; invalid node
    values can still produce misleading success payloads or DB node writes.
  - Required result: invalid node numbers return structured errors and never
    mutate DB node state.

- [x] Implement live console events or explicitly document polling-only v1.
  - Evidence: the plan calls for status-change `event` updates; the current
    service sends a login `snapshot`, while the client polls `stats.get` and
    `nodes.list`.
  - Required result: either push events for node/status changes, or update the
    protocol docs and tests to define polling as the v1 contract.

- [x] Decide whether node locks are runtime-only or persistent.
  - Evidence: lock state is currently an in-memory online registry flag plus a
    `nodes` status upsert. Restarting the daemon loses the lock table.
  - Required result: documented runtime-only semantics, or a persistent node
    lock table/config path with startup restore.

## P1 - Should Finish

- [x] Regenerate checked-in website HTML from the current Markdown docs.
  - Verified: `scripts/build-website.py --output website` regenerated checked-in
    HTML and docs/website consistency tests pass.

- [x] Update `docs/SPEC.md` to the current architecture.
  - Verified: stale default port references were updated and docs consistency
    checks pass against the current console/Buccaneer architecture.

- [x] Retire or rewrite `docker/mutineer.wfc.conf`.
  - Evidence: it still says “requires docker run -it” and sets
    `wfc_enabled=1`, but the local WFC thread has been removed.
  - Required result: either remove this config, or convert it to a console
    service/client example using `console_enabled` and `console_port`.

- [x] Update Docker/Compose console exposure docs and config.
  - Evidence: compose only publishes telnet `2929`; the console service defaults
    to `127.0.0.1:2931` inside the container. `docker/mutineer.docker.conf`
    still says it disables the old local WFC console thread and does not state
    the console-service policy explicitly.
  - Required result: documented host workflow for `mutineer-console` against a
    container, including intentional port publishing or exec/tunnel guidance.

- [x] Normalize runtime helper scripts after the standalone console split.
  - Evidence: `scripts/start-screen`, `scripts/stop`, and `scripts/watchdog`
    still use the screen name `mutineer-console` for the daemon process, which
    now collides conceptually with the real standalone console client.
  - Required result: daemon lifecycle helpers use daemon-oriented names, and any
    WFC/console launcher is clearly a client helper such as `scripts/open-wfc.sh`.

- [x] Make generated test/runtime configs explicit about console service ports.
  - Verified: expect/disposable configs disable the console service when it is
    not under test, and console protocol smoke reserves unique telnet and
    console ports.

- [x] Fix website Markdown rendering before regenerating checked-in HTML.
  - Verified: `scripts/build-website.py --self-test` covers fenced code,
    indented fences, tables, inline code, links, and headings; generated website
    drift checks pass.

- [x] Decide whether checked-in `dist/` release artifacts are source or generated
  output.
  - Verified: `docs/website-source.md` documents Markdown/static HTML as the
    website source of truth and treats release `dist/` package trees as
    generated output verified by package smoke checks.

- [x] Ship `mutineer-console` in release and Docker packages.
  - Evidence: top-level CMake builds the `mutineer-console` target, but
    `scripts/build-release.sh` does not include it in `TOOL_BIN`, does not stage
    `scripts/open-wfc.sh`, and generated release/Docker packages are therefore
    likely missing the standalone console client users now need.
  - Required result: release tarballs and Docker images contain the console
    client plus a working helper script or documented invocation.

- [x] Add release-package smoke coverage for required binaries.
  - Evidence: CI currently lists the tarball contents with `tar tzf ... | head`,
    which can miss omitted tools such as `mutineer-console`.
  - Required result: release CI asserts the package contains `mutineer`,
    `bin/mutineer-console`, core maintenance tools, PLANK/COVE tools, schemas,
    configs, and runtime scripts expected by the docs.

- [x] Fix packaged runtime helper scripts for release-layout installs.
  - Evidence: `scripts/start`, `scripts/start-screen`, `scripts/stop`,
    `scripts/watchdog`, `scripts/backup`, and `scripts/bbs-wall` compute
    `BBS_DIR="$(dirname "$SCRIPT_DIR")/dist"`. That works only after the Makefile
    creates the legacy `dist/mutineer` copy, not when the scripts are shipped
    inside a release tarball whose daemon lives at the package root.
  - Required result: runtime scripts resolve the package root correctly in both
    source-tree and release-tarball layouts, or release packages ship a separate
    install-layout script set.

- [x] Make `scripts/update-version` update the whole installed package, not only
  the daemon binary.
  - Evidence: update install currently replaces `mutineer`, copies `art`, `menus`,
    and `sql`, and optionally applies `migrate.sql`; it does not refresh `bin/`,
    `plank/bin/`, plugins, runtime scripts, `mutineer-console`, or packaged docs.
  - Required result: updates are atomic for all shipped executables/assets, or the
    script is documented as daemon-only and not used for release upgrades.

- [x] Align build/release/Docker dependency manifests with libarchive usage.
  - Evidence: README documents libarchive and the code links archive helpers, but
    GitHub CI, release workflows, Docker builder/runtime dependencies, and
    generated `INSTALL.md` dependency lists omit `libarchive-dev` /
    `libarchive13` or distro equivalents.
  - Required result: every supported build and runtime path declares the archive
    development/runtime package names needed for QWK and file archive commands.

- [x] Complete COVE hub listener protocol handling.
  - Evidence: `coved -mode=hub` opens the hub TCP listener, records accepted
    connections, and immediately closes the socket with a note that protocol
    handling is delegated to PLANK link code.
  - Required result: accepted hub sockets run an actual PLANK link session or the
    docs explicitly mark hub listening as a management/config shell around
    external PLANK transport for now.

- [x] Wire COVE hub auth database into management/API behavior.
  - Evidence: hub config parses `auth_db_path`, but the current management API is
    loopback-only HTTP with no visible auth database initialization or credential
    check.
  - Required result: `auth_db_path` stores real management/node credentials and
    API requests authenticate, or the config key is documented as reserved and
    omitted from production examples until implemented.

- [x] Expand the COVE management API beyond the current thin node/config views.
  - Evidence: current hub mode exposes `/health`, `/config`, `/nodes`,
    `/areas`, `/events`, and `POST /nodes`; it does not expose node disable/delete,
    link health, queue/deadletter inspection, retry controls, auth management, or
    log queries.
  - Required result: management endpoints cover the operational tasks promised by
    the COVE hub docs, with audit entries for each mutating action.

- [x] Align Buccaneer documentation with current dispatch/embedding reality.
  - Verified: host API/programmer docs and generated website output now reflect
    live `USER.FLAGS()` dispatch and CMake-integrated Buccaneer behavior.

- [x] Wire `USER.FLAGS()` through Buccaneer host dispatch or remove it from the
  advertised runtime surface.
  - Evidence: the BBS embedding has a user flags callback, but host dispatch
    does not route `USER.FLAGS`.
  - Required result: `USER.FLAGS()` works and is tested, or docs say it is
    intentionally unavailable.

- [x] Improve Buccaneer BBS host API completeness.
  - Verified: the BBS embedding now binds scoped live KV, data, text, user,
    message, and file APIs through existing DB/filesystem boundaries, with
    Buccaneer host/BBS tests passing.

- [x] Make `BBS.ONLINE()` return real online users for Buccaneer doors.
  - Evidence: current BBS callback returns a new empty array.
  - Required result: returns the same node/session data exposed elsewhere, with
    tests.

- [x] Add Buccaneer chain follow-through or document current “request only”
  behavior.
  - Evidence: `DOOR.CHAIN` sets VM chain state and logs the target, but the door
    launcher does not appear to launch the chained door.
  - Required result: chained door execution works, or docs define chain as a
    reported request for now.

- [x] Clean release-gate compiler warnings.
  - Verified: full `MUTINEER_WARNINGS_AS_ERRORS=ON` build passes, Docker release
    build no longer emits the cleaned menu/chat/COVE/PLANK warnings, and CI no
    longer excludes PLANK store/route/policy tests from sanitizer/fortify runs.

## P2 - Important Polish

- [x] Replace the hand-rolled console JSON parser or tighten malformed JSON
  behavior.
  - Evidence: invalid input such as `not json` currently falls through to
    command/auth handling rather than a distinct `malformed_json` error.
  - Required result: clear parser errors, escaped strings handled consistently,
    and fuzz-style malformed-line tests.

- [x] Add structured console protocol documentation.
  - Verified: `docs/console-protocol.md` documents NDJSON framing, `hello`,
    `login`, request/response shape, snapshots, events, command payloads,
    structured errors, and passthrough begin/end behavior.

- [x] Expand console command behavior tests.
  - Verified: `tests/test_console_protocol.sh` covers stats, nodes, inspect,
    callers, history, logs, system status/shutdown, broadcast, kick, lock,
    unlock, shell denied, malformed JSON, unknown command, and auth paths.

- [x] Make console protocol smoke choose free ports dynamically.
  - Evidence: `tests/test_console_protocol.sh` derives ports from `$$ % 1000`,
    which reduces but does not eliminate parallel-run collisions.
  - Required result: reuse the dynamic free-port helper pattern from the expect
    tests, or otherwise reserve ports before launching the daemon.

- [x] Add a lock-prevents-login integration test.
  - Verified: console protocol smoke locks node 1, opens a telnet connection,
    and verifies runtime assignment skips the locked node; it also verifies
    persistent lock restoration across daemon restart.

- [x] Refresh screenshots after console split.
  - Verified: checked-in screenshot raw/HTML output includes a current
    standalone WFC console capture from `mutineer-console`; docs now describe
    PNG output plus HTML fallback when Chromium is unavailable.

- [x] Bring public website source/build ownership to a single path.
  - Verified: `docs/website-source.md` declares Markdown plus
    `scripts/build-website.py` as canonical, with generated static HTML checked
    in and verified; the Vite/React tree is documented as experimental.

- [x] Add stronger Docker quick-start coverage for console mode.
  - Verified: `tests/docker_quickstart_smoke.sh` full mode builds the image,
    verifies telnet, and verifies the loopback-bound console-control service
    from inside the container.

- [x] Add runtime-script smoke tests against unpacked release tarballs.
  - Verified: `tests/test_release_scripts.sh` builds and unpacks a release
    tarball, asserts required binaries/scripts/assets, syntax-checks packaged
    helpers, runs init, and exercises safe dry-run update paths.

- [x] Add COVE hub-mode smoke tests.
  - Verified: `tests/test_cove_hub.sh` starts `coved -mode=hub`, verifies auth
    and management DB initialization, `/health`, authenticated node mutations,
    malformed request handling, audit events, link health/history, and hub
    socket lifecycle recording.

- [x] Update parity/status files after the console and Buccaneer doc cleanup.
  - Verified: this checklist now reflects the verified console, COVE,
    Buccaneer, release, Docker, website, and warning-gate state after the final
    test pass.

- [x] Broaden docs consistency checks for WFC/console/Buccaneer drift.
  - Verified: docs consistency checks reject stale local-WFC-thread wording,
    stale Buccaneer “not wired” statements, old port drift, and missing package
    docs for the console/COVE/dependency surfaces.

- [x] Clarify shell-script `sqlite3` CLI runtime dependencies.
  - Evidence: core C paths use SQLite through the linked library, but maintenance
    helpers such as `scripts/update-version`, `scripts/backup`, and
    `scripts/bbs-wall` invoke the `sqlite3` command-line tool directly.
  - Required result: docs/package dependencies declare the CLI where needed, or
    scripts move migrations/backups/status queries through maintained native
    helpers.

## Verified Foundations

- [x] Main BBS no longer compiles or starts the old in-process `src/wfc.c`.
- [x] `mutineer-console` builds as a standalone tool target.
- [x] Console service starts alongside telnet and authenticates sysop users.
- [x] Console protocol smoke covers sysop login, bad password, valid non-sysop
  denial, unknown command, malformed unauthenticated input, status, and nodes.
- [x] Main CTest suite passed after completion work: 49/49 tests.
- [x] Node assignment now refuses sessions cleanly when all node slots are busy
  or locked.
- [x] Buccaneer is built by top-level CMake and linked into `mutineer`.
- [x] PLANK/COVE tools are present in the top-level build graph.
