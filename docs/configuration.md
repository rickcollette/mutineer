<!-- generated-by: gsd-doc-writer -->

# Configuration Reference

Mutineer BBS reads a flat key=value configuration file. The canonical struct is `BbsConfig` in `include/bbs_config.h`. Loading is handled by `cfg_load()` in `src/config.c`, which applies defaults then overrides from the file.

Default config path: `conf/mutineer.conf`.

## File Format

```ini
# Comments start with #
key=value
port=2929
bbs_name=Mutineer BBS
```

- One setting per line
- Whitespace around keys and values is trimmed
- Unknown keys are silently ignored
- Boolean-like values use `0` or `1` (integer)

## Environment Variables

All settings come from the config file. There is no automatic `process.env` overlay. Pass an alternate path:

```bash
build/mutineer --config /etc/mutineer/mutineer.conf
```

## Complete Key Reference

Keys match `BbsConfig` fields in `include/bbs_config.h`. Defaults shown are from `cfg_defaults()` in `src/config.c`.

### Network and Listener

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `bind` | string | `0.0.0.0` | No | Address to bind telnet listener |
| `port` | int | `2929` | No | Telnet listen port |
| `idle_timeout_sec` | int | `600` | No | Seconds before idle disconnect |

### Paths

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `db_path` | string | `data/mutineer.db` | Yes* | SQLite database file |
| `data_path` | string | `data` | No | General data directory |
| `logs_path` | string | `logs/mutineer.log` | No | Main log file path |
| `art_path` | string | `art` | No | ANSI/ASCII art directory |
| `menu_main` | string | `menus/main.mnu` | Yes* | Main menu file after login |
| `motd` | string | `art/motd.ans` | No | Message of the day art file |
| `doors_path` | string | `doors` | No | Master door files directory |
| `dropfile_path` | string | `data/dropfiles` | No | Dropfile output directory |
| `protocol_path` | string | `conf/protocols.conf` | No | Transfer protocol definitions |
| `chat_log_path` | string | *(empty)* | No | Chat log directory; empty disables file logging |

\*Startup sanity checks require these paths to exist or be creatable via `mutineer-initbbs`.

### BBS Identity

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `bbs_name` | string | `Mutineer BBS` | No | Display name in MCI `~BN` |
| `sysop_name` | string | `Sysop` | No | Sysop name in MCI `~SN` |

### Session Limits

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `session_time_limit_min` | int | `60` | No | Default per-call time limit (minutes) |
| `max_calls_per_day` | int | `0` | No | Max logins per user per calendar day; `0` = unlimited |
| `max_page_sysop` | int | `3` | No | Max sysop page attempts per session; `0` = unlimited |

### Console / WFC Dashboard

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `console_enabled` | int/bool | `1` | No | Run the TCP console-control service for `mutineer-console` |
| `console_bind` | string | `127.0.0.1` | No | Console-control bind address |
| `console_port` | int | `2931` | No | Console-control TCP port |
| `console_idle_timeout_sec` | int | `600` | No | Idle timeout for console-control clients |
| `wfc_enabled` | int | `0` | No | Deprecated local WFC thread flag; use `mutineer-console` |
| `wfc_refresh_ms` | int | `1000` | No | `mutineer-console` refresh interval (ms) |
| `wfc_blank_sec` | int | `300` | No | `mutineer-console` blank screen timeout; `0` = disabled |
| `wfc_node_num` | int | `1` | No | `mutineer-console` display node number |
| `wfc_fg_color` | int | `11` | No | WFC foreground ANSI color (0–15) |
| `wfc_bg_color` | int | `0` | No | WFC background ANSI color (0–7) |
| `wfc_status_idle_char` | char | `I` | No | Node status char when idle |
| `wfc_status_logging_char` | char | `L` | No | Node status char during login |
| `wfc_status_online_char` | char | `A` | No | Node status char when online |
| `wfc_status_chat_char` | char | `S` | No | Node status char during chat |
| `wfc_shell_enabled` | int/bool | `0` | No | Enable the console `D` command on the server |
| `wfc_shell_command` | string | *(empty)* | No | Server-side argv-template command for console `D`; disabled unless explicitly set |

### Scheduler

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `scheduler_enabled` | int | `1` | No | Run background events thread |
| `scheduler_tick_sec` | int | `30` | No | Scheduler poll interval (seconds) |

### Authentication

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `login_window_sec` | int | `120` | No | Login throttle time window (seconds) |
| `login_max_attempts` | int | `5` | No | Max failed attempts per IP/user per window |
| `password_upgrade` | int | `1` | No | Enable Argon2/bcrypt hash migration on login |
| `password_expire_days` | int | `0` | No | Force password change after N days; `0` = disabled |

### Credits and Ratios

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `default_credits` | int | `5000` | No | Starting credits for new users |
| `default_file_points` | int | `0` | No | Starting file points for new users |

### Multi-Login and Guest

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `allow_multi_login` | int | `0` | No | Allow duplicate logins (`1`) or block (`0`) |
| `guest_enabled` | int | `0` | No | Enable guest account login |
| `guest_handle` | string | `GUEST` | No | Guest account handle |
| `guest_level_id` | int | `1` | No | Security level ID for guests |

### Welcome Letter

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `welcome_letter_enabled` | int | `1` | No | Send welcome letter on first login |
| `welcome_letter_file` | string | `art/welcome.txt` | No | Welcome letter text file path |
| `welcome_letter_from` | string | `Sysop` | No | Sender name on welcome letter |

### DOSBox / DOS Door Runner

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `dosbox_path` | string | `dosbox` | No | Path to DOSBox binary |
| `door_runtime_path` | string | `data/door_runtime` | No | Base dir for per-launch runtime trees |
| `door_copy_mode` | string | `copy` | No | File copy mode for door launch |
| `door_default_timeout_sec` | int | `300` | No | Door timeout; `0` = no timeout |
| `door_cleanup_on_exit` | int | `1` | No | Remove runtime tree on successful exit |
| `door_keep_failed_runs` | int | `0` | No | Keep runtime tree on failure for debugging |
| `door_session_hmac_secret` | string | `mutineer-dev-door-secret` | Yes for production | Shared secret for signing native-door session assertions |

### Plugins

| Key | Type | Default | Required | Description |
|-----|------|---------|----------|-------------|
| `plugins_enabled` | int/bool | `1` | No | Enable plugin loading |
| `plugins_dir` | string | `plugins` | No | Directory containing `.so` plugins |
| `plugins_allowlist` | string | *(empty)* | No | Comma-separated plugin IDs to allow |
| `plugins_denylist` | string | *(empty)* | No | Comma-separated plugin IDs to deny |

## Required vs Optional

**Startup will fail without:**

- Valid config file loadable by `cfg_load()`
- SQLite database at `db_path` with applied schema
- Main menu file at `menu_main`
- Paths validated by `startup_sanity_check()`

**Safe to omit (defaults apply):**

- All WFC tuning keys
- Guest account settings (disabled by default)
- DOS door settings (only needed for DOSBox doors)
- Chat log path (empty disables logging)

## Example Production Config

```ini
bind=0.0.0.0
port=2929
db_path=/var/lib/mutineer/mutineer.db
data_path=/var/lib/mutineer/data
logs_path=/var/log/mutineer/mutineer.log
art_path=/opt/mutineer/art
menu_main=/opt/mutineer/menus/main.mnu
motd=/opt/mutineer/art/motd.ans

bbs_name=Port Royal BBS
sysop_name=Blackbeard

session_time_limit_min=90
idle_timeout_sec=900
login_window_sec=300
login_max_attempts=5
allow_multi_login=0
password_upgrade=1

console_enabled=1
console_bind=127.0.0.1
console_port=2931
wfc_enabled=0
scheduler_enabled=1
scheduler_tick_sec=60

doors_path=/opt/mutineer/doors
dropfile_path=/var/lib/mutineer/dropfiles
dosbox_path=/usr/bin/dosbox
door_runtime_path=/var/lib/mutineer/door_runtime
door_default_timeout_sec=600
door_cleanup_on_exit=1
door_session_hmac_secret=replace-with-a-long-random-production-secret

plugins_enabled=true
plugins_dir=/opt/mutineer/plugins
```

## Per-Environment Overrides

Mutineer does not ship `.env.development` style files. Use separate config files:

```bash
build/mutineer --config conf/mutineer-dev.conf
build/mutineer --config conf/mutineer-prod.conf
```

CLI tools accept `-c` / `--config` with the same keys.

## Related Documentation

- [Getting Started](getting-started.md) — first-time setup
- [Sysop Guide](sysop-guide.md) — WFC and scheduler tuning
- [Console Protocol](console-protocol.md) — console-control TCP framing and commands
- [Doors and Scripting](doors-and-scripting.md) — DOS door settings
- [Plugins](plugins.md) — plugin configuration
