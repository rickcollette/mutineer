# Mutineer BBS Feature Matrix

Review date: 2026-07-13

Archived prior matrix: `archived-feature-matrices/feature-matrix-20260713T230411Z.md`

Scope: fresh deep-dive review after the previous matrix reached 100%. This file lists newly found incomplete, simplified, missing, TODO, or unstable items only. Completed items from the archived matrix are not repeated unless the new review found contradictory code, tests, docs, or tooling.

Review focus:

- Runtime safety in PLANK/COVE storage and Buccaneer host semantics.
- Documentation source-of-truth drift, including generated website pages.
- Test quality gaps where source-string assertions or shell-based test scaffolding can hide regressions.
- Deployment, maintenance, and release tooling that still has placeholder or unsafe behavior.

Second-pass focus:

- Older inline session workflows that still bypass newer command modules.
- Broader PLANK SQL construction problems beyond the first-pass deadletter/quarantine examples.
- File upload staging and telnet/password behavior that were not covered by the previous review.
- Audit/maintenance paths that appear complete at a feature level but still lose data or leave unsafe side effects.

Terminology note: Buccaneer is Mutineer's interpreted language for writing addons, games, doors, and extensions. Low-level implementation files may still use VM/runtime wording for internal bytecode execution, but public/product-facing docs and tooling should not frame Buccaneer as a standalone VM product.

| Priority level | Percentage complete | State | Description | Dependancy |
|---|---:|---|---|---|
| P0 | 100% | Complete | PLANK deadletter storage now builds object-id JSON dynamically, safely handles 100 full object IDs, rejects object lists beyond the supported cap, binds bundle/text values with prepared statements, and is covered by FK-enabled regression tests. | `src/plank/plank_store.c`, `include/bbs_db.h`, `src/db.c`, `tests/plank/test_store.c` |
| P0 | 100% | Complete | PLANK quarantine and related store APIs now use prepared/bound statements for quote-containing node addresses, reasons, peer/link/area/object/import/config/audit values, with runtime tests for quoted text and optional FK fields. | `src/plank/plank_store.c`, `include/bbs_db.h`, `src/db.c`, `tests/plank/test_store.c` |
| P0 | 100% | Complete | Public generated website docs and the hand-authored home page are regenerated/aligned with Buccaneer as Mutineer's interpreted language for addons, games, doors, and extensions; completed `DOOR.CHAIN`, `DOOR.EXIT`, host API, and CAS claims are no longer advertised as open. | `website/`, `README.md`, `docs/overview.md`, `docs/buccaneer/index.md`, `tests/test_docs_consistency.c` |
| P1 | 100% | Complete | Static website table generation now tracks table state explicitly, emits one valid table per Markdown table, supports temp-output validation, and is covered by a CTest website generation check. | `scripts/build-website.py`, `website/docs/*.html`, `CMakeLists.txt` |
| P1 | 100% | Complete | `scripts/create-bucc-github-issues.sh` now reads current `BUCC_TODO.md` follow-up candidates, refuses archived completed Buccaneer issue IDs, supports dry-run preview, and uses interpreted-language framing instead of stale VM-audit wording. | `scripts/create-bucc-github-issues.sh`, `BUCC_TODO.md`, `tests/test_docs_consistency.c` |
| P1 | 100% | Complete | `SHARED.CAS` now uses deep value equality for structured arrays/maps while preserving scalar behavior; map equality in the shared value helper is deep by key/value, and host tests cover scalar match/mismatch plus equal/unequal arrays and maps. | `src/buccaneer/host.c`, `src/buccaneer/value.c`, `src/buccaneer/include/bucc_value.h`, `tests/test_buccaneer_host.c`, `BUCC_TODO.md` |
| P1 | 100% | Complete | Source-string-only coverage for this batch has been replaced or supplemented with executable behavior checks: PLANK store SQL/FK/deadletter tests, website temp-output HTML validation, Buccaneer CAS host dispatch tests, libarchive file-command tests, and updater dry-run failure checks. | `tests/plank/test_store.c`, `tests/test_buccaneer_host.c`, `tests/test_file_cmds.c`, `scripts/build-website.py`, `scripts/update-version` |
| P1 | 100% | Complete | Archive/file command tests no longer shell out to `zip`, `unzip`, `tar`, or `rm -rf`; they use Mutineer's libarchive-backed helpers and `mkdtemp()` under `TMPDIR`, and CTest links the same archive utility code used by production. | `tests/test_file_cmds.c`, `src/archive_util.c`, `src/util.c`, `CMakeLists.txt` |
| P1 | 100% | Complete | PLANK store tests now create isolated `mkstemp()` database paths under `TMPDIR`, initialize both core and PLANK schemas, keep foreign keys enabled, and seed required peer/link parents for deadletter/quarantine/import/audit coverage. | `tests/plank/test_store.c`, `sql/schema.sql`, `sql/plank_schema.sql` |
| P1 | 100% | Complete | Release/update tooling now requires a configured non-placeholder update URL, downloads version/archive/checksum metadata, verifies SHA256 before extraction, rejects unsafe archive entries, extracts into a private temp directory, quotes cleanup traps, and supports a no-network dry-run path. | `scripts/update-version`, release packaging docs |
| P2 | 100% | Complete | Website home page wording is aligned with Markdown docs and docs consistency checks now cover `website/index.html` plus representative generated website pages for stale Buccaneer framing and completed issue claims. | `website/index.html`, `README.md`, `docs/overview.md`, `tests/test_docs_consistency.c` |
| P2 | 100% | Complete | Legacy specification/status docs are aligned with current runtime behavior: protocol execution, archive handling, message/file bases, WFC shell policy, and Buccaneer interpreted-language framing no longer contradict the completed feature matrix, and docs consistency tests cover the legacy docs. | `docs/SPEC.md`, `FUNCTIONAL_MUTINEER.md`, `DELTA_BBS.md`, `TODO.md`, `tests/test_docs_consistency.c` |
| P2 | 100% | Complete | Website generation is now part of CTest through `website_generation`, which builds into a temporary output directory and scans generated HTML for unbalanced/nested tables and stale Buccaneer/completed-issue phrases. | `CMakeLists.txt`, `scripts/build-website.py`, `website/`, `tests/test_docs_consistency.c` |
| P2 | 100% | Complete | Temp-path policy is standardized across expect helpers, transcript/log scripts, PLANK tests, plugin/door/scheduler tests, and C helpers using `TMPDIR` plus `mktemp`/`mkstemp`/`mkdtemp`; regression coverage verifies no fixed `/tmp` dependency remains in the reviewed paths. | `tests/run_expect_tests.sh`, `scripts/test-transcript.exp`, `scripts/test-logfile.exp`, `tests/plank/*.c`, `tests/test_*.c`, `tests/test_temp_paths.c` |
| P3 | 100% | Complete | Repeated C string assembly now uses shared bounded append helpers for message quote/thread formatting, compose/reply/edit bodies, Fido tag append, and file upload extended descriptions, with long-input tests proving truncation is bounded and NUL-terminated. | `include/bbs_util.h`, `src/util.c`, `src/file_cmds.c`, `src/msg_cmds.c`, `src/session.c`, `tests/test_string_builders.c` |
| P0 | 100% | Complete | PLANK store broad raw SQL interpolation has been removed from the peer, link, link-error, area, object, import-history, identity, audit, and config write/read paths touched by this review; prepared DB helper APIs bind text, integer, and BLOB values and quote-containing regression tests cover the write surface. | `src/plank/plank_store.c`, `include/bbs_db.h`, `src/db.c`, `tests/plank/test_store.c` |
| P0 | 100% | Complete | The inline `messages` menu action no longer has a duplicate authorization path; it routes area selection, reading, posting, and replies through `handle_msg_command()` so message-area ACS, area passwords, object reauthorization, and sysop permissions are enforced by the shared message helpers. | `src/session.c`, `src/msg_cmds.c`, `include/bbs_msg_cmds.h`, `menus/message.mnu`, `tests/test_messages.exp` |
| P0 | 100% | Complete | Batch upload staging now uses a private safe temp directory, accepts basename-only regular files, rejects traversal/slashes/symlinks/directories/duplicate destinations, canonicalizes under the target area before moving, and creates DB rows only after the file is safely stored. | `src/file_cmds.c`, `src/db.c`, `include/bbs_util.h`, upload protocol handling, `tests/test_file_cmds.c` |
| P1 | 100% | Complete | The inline `files` menu action no longer leaks or bypasses file policy; it routes through shared file command handlers so area listing, selection, direct download, and batch queue behavior use the same ACS, password, validation, credit, and batch checks as file commands. | `src/session.c`, `src/file_cmds.c`, `include/bbs_file_cmds.h`, `tests/test_files.exp`, file policy tests |
| P1 | 100% | Complete | Telnet password entry now negotiates echo suppression around password prompts, restores echo afterward, tolerates negotiation-only input while reading, and keeps dot masking as fallback/UI behavior; socket tests verify echo-off/restore sequences. | `src/telnet.c`, `include/bbs_telnet.h`, `src/session.c`, `tests/test_telnet_password.c` |
| P1 | 100% | Complete | `plank_store_audit_log()` now binds event fields and optional object/link values, stores object IDs as BLOBs instead of emitting an empty SQL expression, and the PLANK store regression logs an object ID with quote-containing metadata under FK enforcement. | `src/plank/plank_store.c`, `tests/plank/test_store.c` |
| P1 | 100% | Complete | Interactive file purge now previews matching rows/files, requires explicit confirmation, canonicalizes each file under its configured file area, aborts on unsafe paths, coordinates file deletion with DB row removal in a transaction-like flow, and audits deleted/failed counts. | `src/session.c`, `src/db.c`, file area paths, maintenance tests |
| P2 | 100% | Complete | PLANK config getters/setters now use prepared/bound statements for keys and values, and the store regression covers quote-containing keys with integer reads through the public config API. | `src/plank/plank_store.c`, `tests/plank/test_store.c` |
| P2 | 100% | Complete | Process supervision now has disconnect/cancel-aware execution: supervised children run in their own process group, timeout and cancel paths terminate the group promptly, exit cause is reported/logged, and native door/protocol launches pass session fds for mid-transfer disconnect cleanup. | `src/process.c`, `include/bbs_process.h`, `src/doors.c`, `src/file_cmds.c`, `tests/test_process_supervision.c` |

## Validation Snapshot

First-10 completion batch validated on 2026-07-13 with:

- `rm -rf build-first10-non100 && cmake -S . -B build-first10-non100`
- `cmake --build build-first10-non100 -j2`
- `build-first10-non100/test_plank_store`
- `build-first10-non100/test_buccaneer_host`
- `build-first10-non100/test_file_cmds`
- `python3 scripts/build-website.py --check-or-temp-output`
- `scripts/create-bucc-github-issues.sh --dry-run`
- `MUTINEER_UPDATE_URL=https://github.com/yourusername/mutineer scripts/update-version --dry-run` rejected the placeholder URL
- `MUTINEER_UPDATE_URL=file:///tmp/mutineer-release scripts/update-version --dry-run`
- `ctest --test-dir build-first10-non100 --output-on-failure` passed: 42/42 tests
- `rm -rf build-first10-werror && cmake -S . -B build-first10-werror -DMUTINEER_WARNINGS_AS_ERRORS=ON`
- `cmake --build build-first10-werror -j2`

Warnings-as-errors validation passed from `build-first10-werror`.

Remaining-item completion batch validated on 2026-07-14 with:

- `rm -rf build-remaining-non100 && cmake -S . -B build-remaining-non100`
- `cmake --build build-remaining-non100 -j2`
- `build-remaining-non100/test_docs_consistency`
- `build-remaining-non100/test_temp_paths`
- `build-remaining-non100/test_string_builders`
- `build-remaining-non100/test_telnet_password`
- `build-remaining-non100/test_process_supervision`
- `build-remaining-non100/test_file_cmds`
- `build-remaining-non100/test_doors`
- `tests/run_expect_tests.sh build-remaining-non100 errors`
- `tests/run_expect_tests.sh build-remaining-non100 files messages`
- `tests/run_expect_tests.sh build-remaining-non100 plugin`
- `tests/run_expect_tests.sh build-remaining-non100 timebank`
- `ctest --test-dir build-remaining-non100 --output-on-failure` passed: 46/46 tests
- `rm -rf build-remaining-werror && cmake -S . -B build-remaining-werror -DMUTINEER_WARNINGS_AS_ERRORS=ON`
- `cmake --build build-remaining-werror -j2`

Warnings-as-errors validation passed from `build-remaining-werror`.
