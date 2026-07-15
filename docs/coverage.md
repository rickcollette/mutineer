# Documentation Coverage Map

This file is the source-linked map for Mutineer's documentation. It replaces
removed review notes with durable documentation ownership: every runtime
surface, tool, script, schema, and test family should have a stable home under
`docs/`.

## Coverage Policy

- User-facing behavior belongs in operator guides and references.
- Implementation shape belongs in [Architecture](architecture.md) and
  [Developer Guide](developer-guide.md).
- Exact command surfaces belong in [CLI Tools](cli-tools.md), protocol
  references, or subsystem guides.
- The public marketing website is owned by a separate repository; see
  [Website Ownership](website-source.md).
- Review scratchpads and completion checklists should not live under `docs/`.
  Promote lasting facts into the docs below, then delete the scratchpad.

## Core Runtime

| Source | Documentation |
|--------|---------------|
| `src/main.c`, `src/startup.c` | [Overview](overview.md), [Architecture](architecture.md), [Deployment](deployment.md) |
| `src/config.c`, `include/bbs_config.h`, `conf/mutineer.conf` | [Configuration](configuration.md), [Getting Started](getting-started.md) |
| `src/session.c`, `src/telnet.c`, `src/net_listener.c` | [Architecture](architecture.md), [Quick Start](quick-start.md), [Menus and UI](menus-and-ui.md) |
| `src/db.c`, `include/bbs_db.h`, `sql/schema.sql` | [Database Schema](reference/database.md), [Architecture](architecture.md) |
| `src/bbslib.c`, `include/bbslib.h`, `include/bbslib/*` | [bbslib Static SDK](bbslib.md), [Architecture](architecture.md), [Developer Guide](developer-guide.md) |
| `src/auth.c`, `src/hash.c`, `include/bbs_auth.h`, `include/bbs_hash.h` | [Configuration](configuration.md), [Sysop Guide](sysop-guide.md), [Architecture](architecture.md) |
| `src/log.c`, `include/bbs_log.h` | [Sysop Guide](sysop-guide.md), [Configuration](configuration.md) |
| `src/process.c`, `include/bbs_process.h` | [Doors and Scripting](doors-and-scripting.md), [Sysop Guide](sysop-guide.md) |

## Menus, UI, And Templates

| Source | Documentation |
|--------|---------------|
| `src/menu.c`, `include/bbs_menu.h`, `menus/*.mnu` | [Menus and UI](menus-and-ui.md), [Menu Actions](reference/menu-actions.md) |
| `src/menu_template.c`, `include/bbs_menu_template.h`, `art/` | [Menus and UI](menus-and-ui.md), [ACS and MCI](reference/acs-mci.md) |
| `src/mci.c`, `src/acs.c`, `include/bbs_mci.h`, `include/bbs_acs.h` | [ACS and MCI](reference/acs-mci.md), [Menus and UI](menus-and-ui.md) |
| `src/fsedit.c` | [Messages and Mail](messages-and-mail.md), [Sysop Guide](sysop-guide.md) |

## Messages, Files, And Mail

| Source | Documentation |
|--------|---------------|
| `src/msg_cmds.c`, `include/bbs_msg_cmds.h`, `include/bbs_msg_defs.h` | [Messages and Mail](messages-and-mail.md), [Message Commands](reference/message-commands.md) |
| `src/qwk.c`, `include/bbs_qwk.h` | [Messages and Mail](messages-and-mail.md), [CLI Tools](cli-tools.md) |
| `src/fido_netmail.c`, `include/bbs_fido_netmail.h` | [Messages and Mail](messages-and-mail.md), [CLI Tools](cli-tools.md) |
| `src/file_cmds.c`, `include/bbs_file_cmds.h` | [Files and Protocols](files-and-protocols.md), [File Commands](reference/file-commands.md) |
| `src/archive_util.c`, `include/bbs_archive.h` | [Files and Protocols](files-and-protocols.md), [File Commands](reference/file-commands.md) |

## Sysop Console And Operations

| Source | Documentation |
|--------|---------------|
| `src/console_service.c`, `include/bbs_console_service.h` | [Console Protocol](console-protocol.md), [Sysop Guide](sysop-guide.md), [Configuration](configuration.md) |
| `src/tools/mutineer-console.c`, `scripts/open-wfc.sh` | [Console Protocol](console-protocol.md), [CLI Tools](cli-tools.md), [Screenshots](screenshots.md) |
| `src/tools/mutineer-rest.c` | [bbslib Static SDK](bbslib.md), [CLI Tools](cli-tools.md) |
| `src/scheduler.c`, `include/bbs_scheduler.h` | [Sysop Guide](sysop-guide.md), [Configuration](configuration.md) |
| `src/maint.c`, `include/bbs_maint.h`, `src/tools/mutineer-maint.c` | [CLI Tools](cli-tools.md), [Sysop Guide](sysop-guide.md) |
| `scripts/start`, `scripts/start-screen`, `scripts/stop`, `scripts/watchdog` | [Deployment](deployment.md), [Sysop Guide](sysop-guide.md) |
| `scripts/backup`, `scripts/bbs-wall`, `scripts/update-version` | [Deployment](deployment.md), [CLI Tools](cli-tools.md), [Sysop Guide](sysop-guide.md) |

## Doors, Plugins, And Buccaneer

| Source | Documentation |
|--------|---------------|
| `src/doors.c`, `include/bbs_doors.h`, `doors/` | [Doors and Scripting](doors-and-scripting.md), [Configuration](configuration.md) |
| `plugins/hello/hello.c`, `plugins/chat/chat.c` | [Plugins](plugins.md), [Chat and Social](chat-and-social.md) |
| `src/plugin_loader.c`, `src/plugin_registry.c`, `src/plugin_host_api.c` | [Plugins](plugins.md), [Architecture](architecture.md) |
| `include/bbs_plugin_api.h`, `include/bbs_plugin_loader.h`, `include/bbs_plugin_registry.h` | [Plugins](plugins.md), [Developer Guide](developer-guide.md) |
| `src/buccaneer/*`, `src/tools/bucc.c` | [Buccaneer](buccaneer/index.md), [Programmer's Guide](buccaneer/programmers-guide.md), [Host API](buccaneer/host-api.md), [Toolchain](buccaneer/toolchain.md) |
| `SPECS/BUCCANEER/*` | [Buccaneer](buccaneer/index.md), [Door Packages](buccaneer/door-packages.md) |
| Current Buccaneer follow-up candidates | [Buccaneer Follow-Up](buccaneer/follow-up.md) |

## PLANK And COVE

| Source | Documentation |
|--------|---------------|
| `src/plank/*`, `include/plank/*`, `sql/plank_schema.sql` | [PLANK Networking](networking-plank.md), [PLANK Admin](PLANK_ADMIN.md), [Database Schema](reference/database.md) |
| `src/tools/plankd.c`, `src/tools/plankctl.c`, `src/tools/plank-offline.c`, `src/tools/plankpack.c` | [CLI Tools](cli-tools.md), [PLANK Networking](networking-plank.md) |
| `src/tools/coved.c` | [PLANK Networking](networking-plank.md), [CLI Tools](cli-tools.md) |

## Packaging And Automation

| Source | Documentation |
|--------|---------------|
| `docker/Dockerfile`, `docker/compose.yml`, `docker/entrypoint.sh`, `docker/*.conf` | [Deployment](deployment.md), [Quick Start](quick-start.md), [Configuration](configuration.md) |
| `.github/workflows/ci.yml`, `.github/workflows/release.yml` | [Developer Guide](developer-guide.md), [Deployment](deployment.md) |
| `scripts/build-release.sh` | [Deployment](deployment.md), [CLI Tools](cli-tools.md) |
| `scripts/capture-*.sh`, `scripts/ansi-to-screenshots.py`, `screenshots/` | [Screenshots](screenshots.md) |
| `scripts/create-bucc-github-issues.sh` | [Buccaneer Follow-Up](buccaneer/follow-up.md), [Developer Guide](developer-guide.md) |
| External website worktree `../website`, deployment `../production` | [Website Ownership](website-source.md), production deployment docs |

## Test Coverage

| Tests | Documentation |
|-------|---------------|
| `tests/test_*.c`, `tests/plank/test_*.c` | [Developer Guide](developer-guide.md), subsystem docs above |
| `tests/test_bbslib.c` | [bbslib Static SDK](bbslib.md), [Developer Guide](developer-guide.md) |
| `tests/*.exp`, `tests/run_expect_tests.sh` | [Getting Started](getting-started.md), [Developer Guide](developer-guide.md) |
| `tests/test_console_protocol.sh` | [Console Protocol](console-protocol.md), [Developer Guide](developer-guide.md) |
| `tests/test_cove_hub.sh` | [PLANK Networking](networking-plank.md), [Developer Guide](developer-guide.md) |
| `tests/test_release_scripts.sh`, `tests/docker_quickstart_smoke.sh` | [Deployment](deployment.md), [Developer Guide](developer-guide.md) |
| `tests/test_docs_consistency.c`, `tests/test_next15_matrix.c`, `tests/test_first15_matrix.c` | This coverage map, [Parity](PARITY.md), [Developer Guide](developer-guide.md) |

## Validation Snapshot

The current documentation set is expected to describe:

- The standalone `mutineer-console` and JSON console protocol.
- The standalone console model that replaced the embedded WFC implementation.
- Top-level CMake integration for Buccaneer, PLANK, COVE, plugins, and tools.
- Release/package requirements including `sqlite3` CLI and libarchive runtime
  support.
- COVE hub management/auth/link-health behavior.
- Marketing website ownership through the separate website worktree and
  `../production` deployment system.
