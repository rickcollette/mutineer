# Mutineer BBS

A classic telnet BBS server written in C (C11), styled after Renegade BBS with
a green pirate theme. SQLite persistence, modern password hashing, DOSBox DOS
door runner, PLANK store-and-forward networking, and an embedded scripting VM
(Buccaneer).

## Quick start

```bash
# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Initialize fresh database
build/mutineer-initbbs -c conf/mutineer.conf

# Run
build/mutineer -c conf/mutineer.conf
```

Default telnet port: **2929** (set in `conf/mutineer.conf`).

## Dependencies

| Library | Required | Notes |
|---------|----------|-------|
| SQLite3 | Yes | `libsqlite3-dev` |
| OpenSSL | Yes | PBKDF2 password hashing |
| pthreads | Yes | One thread per connection |
| libargon2 | Optional | Argon2 password upgrade path |
| DOSBox | Optional | DOS door runner (`dosbox` on PATH) |

Install on Debian/Ubuntu:
```bash
apt-get install libsqlite3-dev libssl-dev libargon2-dev dosbox
```

## Features

- **Multi-node telnet** — thread-per-connection, up to 256 concurrent nodes
- **Full message system** — threaded boards, private email, QWK offline mail,
  FidoNet netmail export, CC recipients, taglines, full-screen editor
- **File areas** — upload/download, credits/ratio enforcement, batch operations,
  archive test/extract (zip, tar, rar, 7z)
- **Doors** — native doors + DOSBox DOS door runner with per-launch isolation
  and serial nullmodem socket inheritance
- **ACS system** — full expression parser: security levels, AR/AC flags, time,
  credits, conference membership, subscription presence, login counts
- **PLANK networking** — store-and-forward offline packet protocol
- **Buccaneer scripting VM** — embedded bytecode VM for interactive door scripts
- **Sysop tools** — WFC console, remote F-key shortcuts, user/conference/
  protocol/vote/event editors, file validation
- **Color schemes** — 8 named schemes, user-selectable via `pickscheme`
- **Chat** — split-screen two-panel ANSI chat, teleconference, page-with-email-
  fallback, per-session file logging

## Project layout

```
src/           C source — BBS core, session, menus, messages, files, doors, chat
src/tools/     Standalone CLI tools (qwkgen, stats, maint, initbbs, plankctl …)
src/plank/     PLANK networking protocol implementation
src/buccaneer/ Buccaneer scripting language (lexer, parser, VM, host bridge)
include/       Public headers
sql/           SQLite schema and PLANK schema
menus/         Menu definition files (.mnu) and ANSI/ASCII templates
art/           System display art files
plugins/       Example loadable plugins (chat, hello)
doors/         Sample door programs (testdoor — real 16-bit DOS COM)
tests/         Unit and integration tests (ctest)
docs/          Admin guide, parity tracker, template docs, PLANK admin guide
SPECS/         Design specifications (Buccaneer, DOS doors, PLANK)
conf/          Default configuration
```

## Building and testing

```bash
# Configure (Debug build with tests)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build

# Run tests (excludes interactive expect tests)
ctest --test-dir build --exclude-regex "tools_cli|expect_suite"

# Run a specific suite
build/test_doors
build/test_plank_store
```

## Configuration

Edit `conf/mutineer.conf`. Key settings:

```ini
port=2929
db_path=data/mutineer.db
art_path=art
menu_main=menus/main.mnu
sysop_name=Sysop
bbs_name=Mutineer BBS
```

See `docs/admin.md` for the full reference including QWK, DOSBox doors, schema
migration, and all available config keys.

## DOS Doors (DOSBox)

Mutineer supports running classic DOS BBS doors under DOSBox with full serial
nullmodem socket inheritance.

1. Place door files in a master directory.
2. Write a `door.json` manifest (see `doors/testdoor/testdoor.json` for an
   example).
3. Add a `doors` DB record with `runner=dosbox` and `manifest=/path/to/door.json`.

A working test door (`TESTDOOR.COM`, real 16-bit DOS COM program) is included in
`doors/testdoor/`. The Aladdin Adventure DOS door from 1992 is included in
`SPECS/DOSDOORS/aladdin/` with its manifest.

## Buccaneer scripting

Buccaneer is an embedded scripting language for writing interactive BBS doors in
a Pascal-like syntax. The compiler, VM, and host bridge live in `src/buccaneer/`.
See `SPECS/BUCCANEER/Buccaneer_SPEC.md` for the language reference and
`BUCC_TODO.md` for known gaps and open implementation work.

## PLANK networking

PLANK (Packet Link for Area Networked Knowledge) is a store-and-forward message
networking protocol for offline packet exchange between BBS nodes. See
`docs/PLANK_ADMIN.md` and `docs/networking.md`.

## License

See `AGENT.md` for context on this project. No license file has been added yet —
add one before publishing.
