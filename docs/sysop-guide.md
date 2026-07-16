<!-- generated-by: gsd-doc-writer -->

# Sysop Guide

This guide covers day-to-day BBS administration: WFC monitoring, user management, editors, scheduled maintenance, and security practices.

## WFC Console

The Who's On Full Color (WFC) console is now the standalone `mutineer-console`
tool. The main `mutineer` server exposes a loopback console-control TCP port,
and the console logs in with a sysop BBS account.

### Display

WFC uses a responsive notcurses dashboard with live statistic cards, a node
matrix, true-color status accents, modal detail views, and resize handling.
The minimum supported terminal size is 70 by 22 cells.

| Config Key | Default | Meaning |
|------------|---------|---------|
| `wfc_status_idle_char` | `I` | Node idle |
| `wfc_status_logging_char` | `L` | User logging in |
| `wfc_status_online_char` | `A` | User online |
| `wfc_status_chat_char` | `S` | User in chat |

### Tuning

```ini
console_enabled=1
console_bind=127.0.0.1
console_port=2931
console_idle_timeout_sec=600
wfc_refresh_ms=1000
wfc_blank_sec=300
wfc_node_num=1
wfc_fg_color=11
wfc_bg_color=0
```

- `wfc_blank_sec=0` disables screen blanking
- Run `mutineer-console -c conf/mutineer.conf` from an interactive terminal
- Large editor actions run on the server through console passthrough mode

## User Administration

### User Editor (`useredit` action)

Full-screen user record editor for sysops with `+A` AR flag:

- Handle, real name, email, phone, address
- Security level and download security level
- AR and AC flags
- Credits, file points, time bank
- Expiration date, lock/delete status
- Notes and sysop messages

### Security Levels

Managed in `security_levels` table:

| Field | Purpose |
|-------|---------|
| `level` | Numeric security level |
| `time_limit_min` | Per-call time limit |
| `call_allow` | Calls per day |
| `dl_one_day` / `dl_k_one_day` | Download limits |
| `download_ratio_num/den` | Required UL/DL ratio |
| `post_ratio_num/den` | Required post ratio |
| `email_allow` / `vote_allow` / `anon_allow` | Feature permissions |

### Validation Levels

`validation_levels` table defines account validation workflows (new user approval, expiration demotion).

### Guest Account

```ini
guest_enabled=1
guest_handle=GUEST
guest_level_id=1
```

### New User Flow

1. Caller selects New User on logon menu
2. Collects handle, password, profile fields
3. Optional sysop validation queue
4. Welcome letter sent if `welcome_letter_enabled=1`

## Editors

Remote sysop editors accessible via menu actions and F-keys:

| Action | Purpose |
|--------|---------|
| `useredit` | User records |
| `confeditor` | Conferences and membership |
| `areaadmin` | Message areas |
| `fileadmin` | File areas |
| `protocoleditor` | Transfer protocols |
| `menueditor` | Menu files (.mnu) |
| `voteeditor` | Vote topics and choices |
| `eventeditor` | Scheduled events |
| `fidoeditor` | FidoNet AKAs and echolinks |
| `qwkneteditor` | QWK hubs and area links |
| `subscriptioneditor` | Subscription types |

### Menu Editor

Edit menu files in `menus/` directory:

- Add/remove/reorder items
- Set ACS, flags, passwords
- Validate syntax before save

Run `mutineer-validate menus` after external edits.

## Scheduler

Background thread (`scheduler.c`) polls `events` table every `scheduler_tick_sec` (default 30s).

### Event Types

| Type | Trigger |
|------|---------|
| `scheduled` | Cron-like schedule string |
| `logon` | On user login |
| `permission` | ACS-gated event |

### Event Fields

| Field | Purpose |
|-------|---------|
| `name` | Event identifier |
| `schedule` | Schedule expression |
| `command` | Command to execute |
| `acs` | ACS for permission events |
| `warning_min` | Minutes warning before run |
| `enabled` | Active flag |

Example commands: run `mutineer-maint vacuum`, pack tools, external scripts.

Sample crontab entries in `scripts/crontabs`.

## Maintenance

### In-BBS Maintenance (`maintenance` action)

Provides menu for:

- User pack (`mutineer-userpack`)
- Message pack (`mutineer-msgpack`)
- File pack (`mutineer-filepack`)
- Index rebuild
- System statistics display

Implemented in `src/maint.c`.

### CLI Maintenance

```bash
build/mutineer-maint vacuum
build/mutineer-maint reindex
build/mutineer-maint analyze
build/mutineer-maint integrity -v
build/mutineer-maint backup -o /backup/mutineer.db
```

### File Validation

`validatefiles` action scans file areas for inconsistencies between DB and disk.

### Database Backup

```bash
build/mutineer-maint backup -o /path/to/backup.db
# or
scripts/backup
```

Stop BBS or use SQLite backup API for consistent snapshots during heavy load.

## Security

### Password Hashing

- New passwords: PBKDF2 via OpenSSL
- Optional Argon2 upgrade when `password_upgrade=1` and libargon2 linked
- Password expiration: `password_expire_days` (0=disabled)

### Login Throttling

```ini
login_window_sec=120
login_max_attempts=5
```

Tracks failed attempts per IP/user within the window.

### Access Control

- **AR flags** (A–Z): grant capabilities (`+A` = sysop)
- **AC flags**: restrict activities (`RP` = no post, `RM` = no messages)
- **ACS strings**: on menus, areas, doors, events

### Multi-Login

```ini
allow_multi_login=0   # block duplicate sessions
max_calls_per_day=0   # unlimited; set >0 to cap daily logins
```

### Account Status

| Flag | Effect |
|------|---------|
| `STATUS_LOCKED` | Cannot log in |
| `STATUS_DELETED` | Soft deleted |
| `AC_RLOGON` | Restricted from logon |

### Signals

- `SIGUSR1` to BBS process triggers broadcast file check (`broadcast_check()`)
- `SIGINT`/`SIGTERM` graceful shutdown

## Logging

Main log: `logs_path` (default `logs/mutineer.log`).

Log levels via `log.c`: info, warn, error. Door launches, login failures, and plugin events logged.

## Statistics

| Source | Data |
|--------|------|
| `stats` table | Lifetime counters |
| `daily_stats` | Today only |
| `history` | Per-day archive |
| `system_info` | BBS metadata |

```bash
build/mutineer-stats
build/mutineer-stats -j    # JSON output
build/mutineer-stats -s    # key=value short format
```

## Bulletins and Auto Message

| Table | Purpose |
|-------|---------|
| `bulletins` | Sysop bulletins with ACS |
| `automsg` | Single auto-message (id=1) |

Menu action `bulletins` displays bulletins filtered by ACS.

## Voting

| Action | Purpose |
|--------|---------|
| `vote` | User voting booth |
| `voteresults` | View results (sysop/ACS gated) |
| `voteeditor` | Create/edit vote topics |

Tables: `votes`, `vote_choices`, `vote_ballots`, `user_votes`.

## Time Bank

Action `timebank` lets users deposit/withdraw session minutes from `users.timebank` balance.

## Subscriptions

| Action | Purpose |
|--------|---------|
| `subscriptioneditor` | Manage subscription types |
| `subscribe` | User purchase/activate subscription |

ACS term `E#` checks active subscription type.

## Operational Scripts

| Script | Purpose |
|--------|---------|
| `scripts/start` | Start BBS daemon |
| `scripts/stop` | Stop BBS |
| `scripts/start-screen` | Start in screen session |
| `scripts/watchdog` | Auto-restart on crash |
| `scripts/bbs-wall` | Send wall to online users |

## Related Documentation

- [Configuration](configuration.md)
- [Console Protocol](console-protocol.md)
- [CLI Tools](cli-tools.md)
- [Architecture](architecture.md)
- [Reference: Menu Actions](reference/menu-actions.md)
