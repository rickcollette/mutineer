# SPEC — Mutineer Telnet BBS Clone (C)

## 1. Purpose

Build a **Mutineer-style** BBS experience (ANSI + menu-driven) using **telnet sockets** instead of modems, with **multiuser sessions as the default**. Shipping features now include WFC console, event scheduler, ACS/MCI, message/file bases, chat, native/DOSBox/Buccaneer doors, and supervised file-transfer protocols.

This project is a **clean-room** implementation: it recreates the *experience* (menus, message bases, file bases, doors) without copying proprietary assets or source.

## 2. Goals

- Multiuser, node-based server (many simultaneous telnet sessions).
- Telnet-compatible terminal I/O with basic option negotiation.
- Menu system driven by external menu definition files.
- SQLite-backed persistence (users, areas, messages, files, stats, votes, doors, protocols, events).
- “Doors” architecture ready (external programs) with dropfile generation.
- Telnet WFC console for node control.
- Event scheduler backed by `events` table.

## 3. Non-goals

- Full ANSI art pack / exact Mutineer file formats.
- Full door dropfile compatibility with legacy BBSes.
- SSH/TLS natively (use a TLS terminator first).

## 4. Architecture

### 4.1 Process model

**Thread-per-connection** MVP:
- One `pthread` per connected user.
- Each thread owns a `Session`.
- Shared process state (online users list) protected by a mutex.

### 4.2 Key components

- `net_listener`: binds port, accepts connections, starts session threads, handles shutdown flag.
- `telnet`: sends negotiation at connect; parses IAC sequences from input.
- `session`: auth, ACS enforcement, menus, messaging, files, chat, doors, timebank, vote booth.
- `menu`: parses `.mnu` files and renders with MCI.
- `db`: opens SQLite database, initializes schema, exposes helpers for users/nodes/messages/files/votes/doors/protocols/events/stats.
- `wfc`: local console for node list, kick/lock, broadcast.
- `scheduler`: cron-ish runner that executes `events.command` per schedule.
- `doors`: dropfile generation (DOOR.SYS, DORINFO1.DEF) and launcher.

## 5. Telnet behavior

### 5.1 Options negotiated (skeleton)

- `SGA` (Suppress Go Ahead) — improve interactive behavior.
- `ECHO` — server controls echo (password entry can disable).
- `NAWS` — client reports terminal window size.
- `TTYPE` — optional terminal type (xterm, ansi, vt100, etc.)

### 5.2 Input parsing rules

- Telnet control sequences (IAC ...) are removed from the input stream.
- Plain data is passed to the session input accumulator.
- Backspace handling for line input: `0x08` and `0x7F`.

## 6. Menu file format

File extension: `*.mnu`

Each non-empty, non-comment line is:
```
<KEY>|<LABEL>|<ACTION>|<ACS (optional)>
```

- `<KEY>`: single character (case-insensitive)
- `<LABEL>`: displayed text
- `<ACTION>`: routed command (e.g., `who`, `logout`, `help`)
- `<ACS>`: access string; empty allows all. Current support: `Lxx`/`SLxx` numeric level check.

Example:
```
W|Who's Online|who|
Q|Logoff|logout|
```

## 7. Database

### 7.1 Storage choice
SQLite3 for ease of deployment and correctness via transactions.

### 7.2 Schema
The canonical schema lives in `sql/schema.sql` and is applied with `CREATE TABLE IF NOT EXISTS ...` on startup.

## 8. Security baseline
- Password hashing with PBKDF2 + Argon2 upgrade path; bcrypt if libcrypt present.
- Login attempt throttling per IP/user.
- ACS with precedence, AR flags, credits/ratio/time checks.
- Audit log + trap file.
- Optional TLS front-end and/or SSH via external terminator (see networking.md).

## 9. Build & run

- Build with CMake.
- SQLite3 development headers/libs are required; the daemon will not build without SQLite persistence.

Run:
```
./mutineer --config conf/mutineer.conf
```

Connect:
```
telnet 127.0.0.1 2929
```

## 10. Roadmap (high level)

- Complete green art pack and menu trees.
- Netmail/FTN export pipeline; archive/virus-scan hooks.
- Rich protocol/batch UX and split-screen chat UI polish.
- CI: golden transcripts, load tests, sanitizers.
- Scale: migrate to event loop (epoll/kqueue) once features stabilize.

## 11. Config (Mutineer)
- `port`, `bind`
- `db_path`, `data_path`
- `logs_path` (default `logs/mutineer.log`)
- `art_path`, `motd`
- `menu_main`
- `idle_timeout_sec`
- `session_time_limit_min`
- `wfc_enabled`, `wfc_refresh_ms`
- `scheduler_enabled`, `scheduler_tick_sec`
- `login_window_sec`, `login_max_attempts`, `password_upgrade`
- `default_credits`, `default_file_points`
- `doors_path`, `dropfile_path`, `protocol_path`
