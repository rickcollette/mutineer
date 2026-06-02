# Admin Guide (Mutineer)

## Configuration

Edit `conf/mutineer.conf`. Key sections:

```
# Core
bind=0.0.0.0
port=2323
db_path=data/mutineer.db
art_path=art
menu_main=main.mnu
session_time_limit_min=60
idle_timeout_sec=300

# Paths
dropfile_path=data/dropfiles
protocol_path=data/protocols
door_runtime_path=data/door_runtime

# Auth
login_window_sec=300
login_max_attempts=5
password_expire_days=0          # 0=disabled
max_calls_per_day=0             # 0=unlimited

# Chat
max_page_sysop=3                # max sysop pages per session
chat_log_path=                  # empty=disabled

# DOSBox doors
dosbox_path=dosbox
door_default_timeout_sec=300
door_cleanup_on_exit=1
door_keep_failed_runs=0
```

## Database and Schema Migration

The database is SQLite at `db_path`. Schema lives in `sql/schema.sql`.

- **Fresh install**: `mutineer-initbbs -c conf/mutineer.conf` runs `db_init_schema()` and seeds defaults.
- **Upgrades**: `db_apply_core_migrations()` runs automatically on startup. It uses `ALTER TABLE … ADD COLUMN IF NOT EXISTS` so existing data is preserved. No manual migration steps are needed.
- **Backup**: `mutineer-maint -c conf/mutineer.conf --backup` runs VACUUM INTO a dated copy.

## Art and Display Files

Place ANSI files under `art/`. Both `.ans` (ANSI color) and `.asc` (plain text) variants are used; `.ans` is shown to ANSI-capable callers, `.asc` as fallback.

System display files (shown automatically at the matching event):

| File | Event |
|------|-------|
| `NOACCESS.ans/.asc` | ACS check failed |
| `2MANYCAL.ans/.asc` | Daily call limit reached |
| `NOTLEFTA.ans/.asc` | Session time expired |
| `NOCREDTS.ans/.asc` | Insufficient credits |
| `PWCHANGE.ans/.asc` | Password change required |
| `welcome.ans/.asc` | After login |
| `goodbye.ans/.asc` | On logout |
| `newuser.ans/.asc` | After new user registration |
| `multilog.ans` | Duplicate login blocked |
| `motd.ans` | Message of the day |

MCI tokens supported in art files: `%UN` (handle), `%TL` (time left), `%PC` (credits), `%MT` (messages), `%FT` (files), `%NO` (online count), and many more — see `docs/MCI.md`.

## Menus

`.mnu` files live in `menus/`. Format: `KEY|LABEL|ACTION|ACS`

Template files `.ans`/`.asc` in the same directory control visual layout. Placeholders: `%%MENU_ITEMS%%`, `%%PROMPT%%`, `%%TITLE%%`.

Key actions: `messages`, `files`, `chat`, `bulletins`, `page`, `wall`, `whisper`, `doors`, `vote`, `timebank`, `subscriptioneditor`, `pickscheme`, `togglefse`, `useredit`, `confeditor`, `protocoleditor`, `menueditor`, `voteeditor`, `eventeditor`, `validatefiles`, `logout`.

## QWK Mail Workflow

QWK allows users to download a compressed mail packet, compose replies offline, and upload the reply packet.

**Generating a packet (sysop / scheduled):**
```bash
mutineer-qwkgen -c conf/mutineer.conf --user johndoe --output /tmp/johndoe.qwk
```

**User session flow:**
1. User selects QWK Download from the message menu.
2. BBS generates the packet (MESSAGES.DAT, *.NDX, NEWFILES.DAT, CONTROL.DAT) and initiates a Zmodem/Ymodem download.
3. User reads/replies in their offline mail reader.
4. User uploads the reply `.qwk` packet.
5. BBS imports the replies into the message base.

**Scheduling packet generation:**
```
# In conf/mutineer.conf or via Event Editor:
# Add an event with schedule "daily@03:00" and command "mutineer-qwkgen --all-users"
```

## DOS Door (DOSBox) Setup

1. Place door files in a master directory, e.g. `/opt/doors/aladdin/`.
2. Create a JSON manifest:

```json
{
  "runner": "dosbox",
  "name": "aladdin",
  "master_dir": "/opt/doors/aladdin",
  "startup": "ALADDIN.EXE",
  "dropfile": "DOOR.SYS",
  "dropfile_dest": "game",
  "machine": "svga_s3",
  "memsize": 16,
  "core": "auto",
  "cycles": "3000",
  "timeout_sec": 600,
  "cleanup_on_exit": true
}
```

3. Add a door record (via sysop door editor or SQL):
   - `runner`: `dosbox`
   - `manifest`: `/opt/doors/aladdin/aladdin.json`
   - `enabled`: 1

4. Wire it into `menus/door.mnu`:
   ```
   A|Aladdin Adventure|door_1|S10
   ```

A sample test door (`doors/testdoor/TESTDOOR.COM` + `testdoor.json`) is included for verifying the pipeline end-to-end.

## Sysop Remote F-Key Shortcuts

When connected remotely as a sysop (`+A` AR flag), F-keys trigger shortcuts from the main menu:

| Key | Action |
|-----|--------|
| F1 | Who's online |
| F2 | Broadcast message |
| F3 | Kick a node |
| F4 | System status |
| F8 | Twit user (disconnect silently) |
| F10 | Initiate split-screen chat with node |

Sequences recognized: VT100 (`ESC O P/Q/R/S`), xterm (`ESC [ 1 1 ~` etc.), Linux console (`ESC [ [ A/B/C/D/E`).

## Command-line Tools

| Tool | Purpose |
|------|---------|
| `mutineer-initbbs` | Initialize a fresh database with schema + defaults |
| `mutineer-qwkgen` | Generate QWK packet for one or all users |
| `mutineer-msgpack` | Purge old messages (by age or count) |
| `mutineer-userpack` | Remove deleted/inactive user records |
| `mutineer-filepack` | Remove orphaned file records (no physical file) |
| `mutineer-maint` | VACUUM, reindex, backup database |
| `mutineer-stats` | Print system statistics (text or JSON) |
| `mutineer-validate` | Validate schema, config, and file areas |
| `plankctl` | Admin tool for PLANK network (links, queue, audit) |
| `plank-offline` | Export/import offline PLANK packets for users |
| `plankd` | PLANK link daemon (background networking) |

Run any tool with `--help` for full options.

## WFC Console Hotkeys

Local hotkeys at the WFC (Waiting for Caller) screen:

| Key | Action |
|-----|--------|
| `k<N>` | Kick node N |
| `l<N>` | Lock node N (prevent new logins) |
| `b<msg>` | Broadcast message to all nodes |
| `q` | Shutdown BBS |
| `@<pos>` | Inspect node (e.g. `@A0`) |
| F1 | Quit WFC |
| F4 | Refresh screen |

## Security Notes

- Default security level for new users is `1`. Sysops should have AR flag `A` set.
- ACS expressions support: `S#`, `D#`, `F?`, `R?`, `A#`, `B#`, `C#`, `E#`, `G?`, `H#`, `J#`, `N#`, `P#`, `T#`, `U#`, `V`, `W#`, `X#`, `PC`, `DR`, `Z`, and legacy forms. See `docs/ACS_MCI.md`.
- Passwords are hashed with PBKDF2 (SHA-256) and upgraded to Argon2 on first login if `libargon2` is present.
- Rate limiting: `login_window_sec` / `login_max_attempts` per IP/user pair.
