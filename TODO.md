# TODO

## Overview

This file represents the full repository backlog for Mutineer BBS. It covers current work needed across core persistence, session/menu UI, QWK/netmail/file transfer, PLANK networking, plugin/door support, documentation, and testing.

## Current status

- PLANK offline packet export/list/status and plankctl query-backed inspection support are implemented; current status and validation snapshots are tracked in `feature-matrix.md`.
- QWK upload/download and FidoNet netmail outbound export now use real persisted/transfer paths.
- Database persistence requires SQLite and applies schema/migration checks explicitly.
- Multi-login prevention, password expiration/recovery, guest account, vote results display, subscription management, last callers display, and event scheduling (daily/weekly/monthly/logon) are all implemented.
- ACS conditions `PC` (post/call ratio) and `DR` (download ratio) added to `src/acs.c`.
- File commands FP (set scan date), FT (archive test), FQ (archive extract), FK (batch remove), FJ (batch upload) implemented in `src/file_cmds.c`; wired as session actions and added to `menus/file.mnu`.
- WFC console unknown-command hint added at `src/wfc.c:1244`.
- The DOSBox door runner, art pack, display files, and documentation parity work listed in earlier sections have current completion status in `feature-matrix.md`.

## 1. Build & persistence

- [x] Require SQLite support in the build or provide a real embedded DB fallback.
- [x] Remove `db->stub` fallback modes and make the database path real by default.
- [x] Harden DB initialization, schema migration, and error handling in `src/db.c`.
- [x] Verify schema loading works for both fresh installs and upgrades.

## 2. Core session, menu, and UI

- [x] Implement menu editor save and deletion functionality in `src/session.c` and `src/menu.c`.
- [x] Audit menu/template persistence and ensure saved menu edits persist correctly.
- [x] Validate placeholder handling in `src/menu_template.c` and update docs accordingly.
- [x] Add WFC status/console handling and make node status configurable instead of hard-coded.
- [x] Remove any user-facing placeholder/hard-coded session values.

## 3. QWK, netmail, and file transfer

- [x] Replace QWK transfer simulation in `src/qwk.c` with actual file transfer support.
- [x] Implement or wire real Zmodem/Ymodem/Xmodem upload and download behavior.
- [x] Complete the QWK packet generation path and ensure exported packets are deliverable.
- [x] Implement reply packet upload/import command flow without requiring manual file staging.
- [x] Verify netmail export and processing paths are fully functional.
- [x] Add `*.NDX` conference index files to QWK packet output.
- [x] Add `NEWFILES.DAT` (new files list) to QWK packet output.
- [x] Honor per-user max-messages-per-area setting in QWK generation.

## 4. PLANK networking and tools

- [x] Implement `src/tools/plank-offline.c` export functionality using `plank_bundle_export_user_packet()`.
- [x] Implement `src/tools/plank-offline.c` listing and status queries for pending exports, user export history.
- [x] Clean up `src/plank/plank_store.c` SQL binding with safe string escaping in audit log and deadletter functions.
- [x] Add proper message filtering to `plank_bundle_export_user_packet()` semantics: read state and message limits.
- [x] Validate `plank_bundle_import_reply()` import behavior and reply history tracking.
- [x] Ensure `plankctl` provides reliable query-backed output for all command internals.
- [x] Review and complete PLANK schema coverage for export/reply history and dedupe tables.

## 5. Plugin, door, and BUCC support

- [x] Implement plugin commands advertised but not supported, such as `/who` in `plugins/chat/chat.c`.
- [x] Review BUCC host function support in `src/buccaneer/` and `src/doors.c`.
- [x] Ensure door/NSS function calls are implemented or rejected cleanly with guidance.
- [x] Implement DOSBox door runner per `SPECS/DOSDOORS/DOS_DOOR_CURSOR_PROMPT.md`:
  - Refactor `door_launch()` in `src/doors.c` into a dispatcher with `door_launch_native()` and `door_launch_dosbox()`.
  - Extend the `doors` DB table with `runner`, `manifest`, `enabled`, `timeout_sec` columns and a schema migration.
  - Implement a JSON manifest parser for DOSBox doors (fields: `runner`, `name`, `master_dir`, `startup`, `dropfile`, `dropfile_dest`, `machine`, `memsize`, `core`, `cycles`, `serial_telnet`, `usedtr`, `timeout_sec`, `copy_mode`, `cleanup_on_exit`).
  - Add config keys: `dosbox_path`, `door_runtime_path`, `door_copy_mode`, `door_default_timeout_sec`, `door_cleanup_on_exit`, `door_keep_failed_runs`.
  - Create per-launch isolated runtime tree (`<door_runtime_path>/<name>/node<NN>/<launch-id>/game`).
  - Generate a per-run DOSBox config with `serial1=nullmodem inhsocket:1 telnet:1` and an autoexec that mounts only the runtime `game/` directory.
  - Launch DOSBox via `fork()`/`execv()` passing `-conf <conf> -socket <fd> -exit`; never use `system()`.
  - Supervise the child, enforce timeout, update node activity to `door:<name>`, restore on exit.
  - Handle disconnect/shutdown: terminate child cleanly, force-kill if needed, no orphan processes.
  - Add tests using `SPECS/DOSDOORS/aladdin/` as the reference DOS door (fake-executable fallback for CI).

## 6. File area completions

- [x] Implement `FP` (Set New Scan Date) — `cmd_file_set_scan_date()` in `src/file_cmds.c`; overrides `last_login_at` for the session's `FN` new-scan via `s->file_scan_date`.
- [x] Implement batch remove (`FK`) and batch upload (`FJ`) — `cmd_file_batch_remove()` and `cmd_file_batch_upload()` in `src/file_cmds.c`; wired as `batchremove`/`batchupload` session actions.
- [x] Implement archive test (`FT`) and archive extract (`FQ`) — `cmd_file_archive_test()` and `cmd_file_archive_extract()` in `src/file_cmds.c`; wired as `archivetest`/`archiveextract` session actions.
- [x] All new file commands exposed in `menus/file.mnu`.

## 7. Message system completions

- [x] Implement `MZ` (Toggle Scan Flags) — lets users configure which areas are included in new-message scans.
- [x] Implement carbon copy (CC) recipients for email/private messages.
- [x] Implement tagline/signature support for posted messages (per-user configurable).
- [x] Enforce mailbox capacity limits (max messages per user inbox).
- [x] Implement auto-save draft on disconnect (save partial composed message to drafts table).
- [x] Implement full-screen message editor (`FSEditor` flag per user).
- [x] Wire FidoNet echomail import/export — DB schema (`fido_echolinks`) and `db_fido_echolink_add()` exist but the import/export tosser integration and echomail area type (`type=1`) are not wired in `src/fido_netmail.c`.

## 8. Sysop editors and admin tools

- [x] Implement Menu Editor (visual/interactive editing of `.mnu` files from within the BBS session) — `docs/PARITY.md` lists this as partial.
- [x] Implement Protocol Editor (`*X` menu command) — configure transfer protocols interactively.
- [x] Implement Conference Editor (`*R` menu command) — manage message conference areas.
- [x] Implement remote sysop F-key shortcuts — F1=who, F2=broadcast, F3=kick, F4=stats, F8=twit, F10=chat. VT100/ANSI/xterm sequences parsed; sysop-only guard. `parse_fkey()` + `handle_fkey()` in `src/session.c`.
- [x] Add `*7` (Validate Files) sysop command.
- [x] Add `*E` (Event Editor), `*V` (Vote Editor) sysop commands.

## 9. ACS system completions

- [x] Add `PC` (post/call ratio met) and `DR` (download ratio met) ACS conditions to `src/acs.c`.
- [x] Add `C<n>` (calls/logins >= n) ACS condition — currently `C?` handles conference letters only; a numeric `C#` form for "total logins >= n" is absent.
- [x] Add a subscription-presence ACS condition (e.g. `E#` — has active subscription of type #) — `J<n>` (conference) is already implemented; subscriptions are stored in DB but no ACS code checks them.

## 10. Display files and art pack

- [x] Create missing system display files in `art/`: `NOACCESS` (menu access denied), `2MANYCAL` (too many calls today), `NOTLEFTA` (no time left), `NOCREDTS` (insufficient credits), `PWCHANGE` (password change required). Both `.ans` and `.asc` variants needed.
- [x] Complete the green-themed ANSI art pack — all menu templates (main, message, file, door, chat, sysop, logon, vote) updated to consistent green/cyan pirate theme; `vote.ans/.asc/.mnu` added; 22/22 template tests pass.
- [x] Implement color scheme management — 8 named built-in schemes defined in `src/mci.c`; `^0`-`^9` tokens use per-scheme color table; `pickscheme` session action for user selection.

## 11. Chat improvements

- [x] Add per-user chat session logging to a file (configurable opt-in) — `chat_log_path` config key; `chat_log_file()` in `src/chat.c`.
- [x] Improve split-screen chat UI — replaced rapid-refresh hack with proper two-panel ANSI scroll-region layout; mutex-protected inter-session inbox queue; top panel for remote, bottom for local; scroll regions prevent flicker.
- [x] Add configurable maximum paging attempts per session — `max_page_sysop` config key; enforced in `session.c` page action.
- [x] Offer email fallback when sysop is unavailable for chat — offered after page limit reached and after each page attempt.

## 12. WFC console

- [x] `src/wfc.c` default case now prints a one-line key-reference hint instead of silently dropping unrecognized input.

## 13. Documentation and parity

- [x] Update `docs/PARITY.md` to reflect all features completed: ACS, F-keys, DOSBox, art files, color schemes, sysop editors, chat improvements.
- [x] Remove stale or incorrect claims — PARITY.md rewritten with accurate status per feature.
- [x] Add usage documentation for `plankctl`, `plank-offline`, QWK workflows, configuration, doors, and schema migration — `docs/admin.md` rewritten comprehensively.
- [x] Document database and schema migration requirements — auto-migration via `db_apply_core_migrations()` documented in `docs/admin.md`.
- [x] Update `DELTA_BBS.md` summary table and ACS section; `2.3` authentication features updated to reflect multi-login, password expiry, guest, subscriptions, daily limits.

## 14. Quality, testing, and cleanup

- [x] Add regression tests for PLANK store: identity, config, object put/exists, dedupe, transactions, deadletter, quarantine, audit log — `tests/plank/test_store.c`, 10/10 pass. (Also fixed `plank_store_get_identity` SQL bug: `node_addr` column was referenced but not in schema.)
- [x] Add tests for menu template validation and placeholder handling — `tests/test_menu_template.c`, 22/22 pass.
- [x] Add tests for the DOSBox door runner (manifest parsing, runtime directory creation, argv generation, timeout, node activity transitions, native door regression) — 49 tests in `tests/test_doors.c`, all pass.
- [x] Add tests for archive command hooks — `tests/test_file_cmds.c`: extension detection, temp dir isolation, zip/tar test+extract via real shell tools. 27/27 pass.
- [x] Validate the full build and runtime experience with SQLite enabled — all targets build cleanly; 18/18 unit+integration tests pass via ctest. Fixed 3 pre-existing PLANK bugs: `plank_store_get_identity` SQL missing column, `plank_bundle_reader_open` skipping manifest on empty bundles, `plank_bundle_reader_verify` swapped signature/pubkey args.
- [x] Add a smoke test command that verifies startup → config → DB → listener → basic session flow end-to-end — `tests/test_smoke.c`, 4/4 tests pass.

## Notes

- This TODO is intentionally repository-wide, not limited to the PLANK subsystem.
- Prioritize real end-to-end feature completion rather than leaving user-facing flows simulated.
- Commands and UI pathways that are advertised should be implemented or explicitly disabled.
- The DOSBox door runner (section 5) is the single largest outstanding feature spec; `SPECS/DOSDOORS/DOS_DOOR_CURSOR_PROMPT.md` is the authoritative design document.
