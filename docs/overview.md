<!-- generated-by: gsd-doc-writer -->

# Mutineer BBS Overview

Mutineer BBS is a classic telnet bulletin board system for Linux, written in C11. It targets operators who want Renegade-style menus, message and file areas, doors, and sysop tools — with modern persistence (SQLite), password hashing (PBKDF2 with optional Argon2 upgrade), and extensibility via loadable plugins and the Buccaneer scripting VM.

## Design Goals

- **Classic BBS UX** — ANSI menus, MCI template expansion, ACS access control, hotkeys, and F-key sysop shortcuts.
- **Thread-per-connection** — Up to 256 concurrent telnet nodes without blocking the main listener.
- **SQLite everywhere** — Users, messages, files, doors, votes, events, QWK/FidoNet queues, and PLANK objects share one database.
- **Safe door execution** — Native doors via `fork`/`exec`; DOS doors via isolated DOSBox runtime trees with serial nullmodem socket inheritance.
- **Offline networking** — PLANK store-and-forward packets; QWK offline mail; FidoNet netmail export.

## Feature Summary

### Connectivity and Sessions

| Feature | Description |
|---------|-------------|
| Multi-node telnet | Thread-per-connection on configurable bind/port (default `0.0.0.0:2929`) |
| Login throttling | Per-IP/user attempt limits via `login_window_sec` / `login_max_attempts` |
| Guest account | Optional guest login with configurable handle and security level |
| Multi-login control | Block or allow duplicate logins per user |
| Idle timeout | Disconnect inactive sessions after `idle_timeout_sec` |
| Session time limit | Per-call minute limit from security level or config default |

### Messages and Mail

- Threaded message areas with ACS read/post/sysop controls
- Private email with CC, taglines, signatures, full-screen editor
- M*/R* command set (Renegade-compatible two-letter commands)
- QWK offline mail pack generation and upload
- FidoNet netmail export and echomail area linking
- Conference membership with ACS `J#` and `C?` terms
- Message drafts saved on disconnect during composition

### Files and Downloads

- Multiple file areas with per-area ACS and flags
- Upload/download with credit and ratio enforcement
- Batch download queue (FB/FC/FK commands)
- Archive test/extract/view (zip, tar, rar, 7z via external tools)
- SHA-256 duplicate detection on upload
- Protocol launcher for configured transfer protocols

### Doors and Scripting

- Native executable doors with dropfile generation (DOOR.SYS, DORINFO1.DEF)
- DOSBox DOS door runner with per-launch isolation
- Buccaneer embedded bytecode VM (Pascal-like syntax)
- Loadable `.so` plugins via `dlopen` with stable ABI

### Sysop Tools

- WFC (Who's On Full Color) console with node status grid
- Remote editors: users, conferences, protocols, votes, events, menus, Fido/QWK
- File validation and maintenance commands
- Scheduler thread for cron-like events
- Color scheme picker (8 named schemes)

### Chat and Social

- Split-screen two-panel ANSI chat between nodes
- Teleconference rooms with optional passwords
- Sysop paging with email fallback
- Wall broadcast and node whisper
- Oneliners and short messages (SMW)
- Optional per-session chat file logging

### Networking

- **PLANK** — Store-and-forward CBOR object protocol with Ed25519 signing
- **QWK** — Hub-linked area import/export with packet queue
- **FidoNet** — AKA management, echomail links, netmail queue

## Project Layout

```
mutineer-bbs/
├── src/                 BBS core (session, menus, messages, files, doors, chat)
│   ├── tools/           Standalone CLI utilities
│   ├── plank/           PLANK protocol implementation
│   └── buccaneer/       Buccaneer language (lexer, parser, VM, host bridge)
├── include/             Public headers
├── sql/                 schema.sql (BBS) + plank_schema.sql (PLANK extension)
├── conf/                Default configuration (mutineer.conf)
├── menus/               Menu definitions (.mnu) and ANSI/ASCII templates
├── art/                 System display art files
├── plugins/             Example loadable plugins (hello, chat)
├── doors/               Sample door programs (testdoor — 16-bit DOS COM)
├── tests/               Unit tests (ctest) and expect integration tests
├── scripts/             start/stop/backup/watchdog shell helpers
├── docs/                This documentation set
└── SPECS/               Design specifications
```

## Binaries

Built by CMake into `build/` (or your chosen build directory):

### Main Server

| Binary | Purpose |
|--------|---------|
| `mutineer` | Main BBS telnet server |

### BBS CLI Tools

| Binary | Purpose |
|--------|---------|
| `mutineer-initbbs` | Initialize database, directories, sanity checks |
| `mutineer-maint` | vacuum, reindex, analyze, integrity, backup |
| `mutineer-stats` | Display system statistics (normal/JSON/short) |
| `mutineer-qwkgen` | Generate QWK offline mail packets |
| `mutineer-msgpack` | Pack/prune message areas |
| `mutineer-userpack` | Pack/prune user records |
| `mutineer-filepack` | Pack/prune file areas |
| `mutineer-netmail-export` | Export FidoNet netmail queue |
| `mutineer-validate` | Validate menu/template files |

### PLANK Tools

| Binary | Purpose |
|--------|---------|
| `plankd` | PLANK link daemon (outbound/inbound sync) |
| `coved` | COVE hub daemon |
| `plankctl` | Administrative control (status, peers, links, queue) |
| `plankpack` | Bundle pack/unpack for offline exchange |
| `plank-offline` | Offline import/export operations |

### Buccaneer Tools

Built from `src/buccaneer/Makefile`:

| Binary | Purpose |
|--------|---------|
| `bucc` | Buccaneer compiler |
| `bucc-linter` | Static analysis |
| `bucc-formatter` | Source formatter |
| `bucc-simulator` | Standalone VM simulator |

### Test Binaries

`test_acs`, `test_doors`, `test_mci`, `test_menu`, `test_menu_template`, `test_smoke`, `test_tools`, `test_file_cmds`, `test_plugin`, and `test_plank_*` suites.

## Dependencies

| Library / Tool | Required | Notes |
|----------------|----------|-------|
| SQLite3 | Yes | `libsqlite3-dev` — persistent storage |
| OpenSSL | Yes | `libssl-dev` — PBKDF2 password hashing, PLANK crypto |
| pthreads | Yes | Thread-per-connection model |
| libargon2 | Optional | Argon2 password upgrade when `password_upgrade=1` |
| libzstd | Optional | PLANK bundle compression |
| DOSBox | Optional | DOS door runner (`dosbox` on PATH or `dosbox_path`) |
| zip/unzip, tar, unrar, 7z | Optional | Archive operations in file commands |

Debian/Ubuntu install:

```bash
apt-get install libsqlite3-dev libssl-dev libargon2-dev dosbox
```

## Configuration Entry Point

Primary config file: `conf/mutineer.conf` (key=value format). See [Configuration](configuration.md) for all keys.

Key paths:

```ini
port=2929
db_path=data/mutineer.db
menu_main=menus/main.mnu
art_path=art
bbs_name=Mutineer BBS
sysop_name=Sysop
```

## Documentation Map

- New users: [Getting Started](getting-started.md)
- Operators: [Sysop Guide](sysop-guide.md), [Configuration](configuration.md)
- Developers: [Developer Guide](developer-guide.md), [Architecture](architecture.md)
- Command reference: [reference/](reference/)
