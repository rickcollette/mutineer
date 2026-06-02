<!-- generated-by: gsd-doc-writer -->

# CLI Tools

Mutineer ships standalone command-line utilities for initialization, maintenance, statistics, offline mail, and PLANK networking. All tools share `db.c`, `config.c`, and related core sources.

Build all BBS tools:

```bash
cmake --build build --target tools
```

Build PLANK tools:

```bash
cmake --build build --target plank
```

Default config path: `conf/mutineer.conf` (most tools accept `-c` override).

## BBS Tools

### mutineer-initbbs

Initialize database, directories, and seed data.

```bash
mutineer-initbbs [options]

Options:
  -c, --config <path>   Config file (default: conf/mutineer.conf)
  -y, --yes             Auto-create all missing items
  -n, --dry-run         Report only, don't create
  -v, --verbose         Verbose output
  -h, --help            Show help
```

Exit codes: 0=OK, 1=bad args, 2=missing (dry-run), 3=user declined, 4=create failed.

Examples:

```bash
mutineer-initbbs -y
mutineer-initbbs -n -v
mutineer-initbbs -c /etc/mutineer.conf -y
```

### mutineer-maint

Database maintenance.

```bash
mutineer-maint [options] <command>

Commands:
  vacuum      Reclaim disk space
  reindex     Rebuild indexes
  analyze     Update query planner statistics
  integrity   Check database integrity
  backup      Create database backup

Options:
  -c, --config <path>
  -o, --output <path>   Backup destination
  -v, --verbose
  -h, --help
```

Examples:

```bash
mutineer-maint vacuum
mutineer-maint backup -o /backup/mutineer-$(date +%Y%m%d).db
mutineer-maint integrity -v
```

### mutineer-stats

Display system statistics.

```bash
mutineer-stats [options]

Options:
  -c, --config <path>
  -j, --json            JSON output
  -s, --short           key=value pairs
  -h, --help
```

Reports calls, uploads, downloads, posts, emails from `stats`, `daily_stats`, and `system_info` tables.

### mutineer-qwkgen

Generate QWK offline mail packets.

```bash
mutineer-qwkgen [options]
  -c, --config <path>
  -u, --user <handle>   Target user
  -h, --help
```

Used by sysops/cron for offline mail distribution.

### mutineer-msgpack

Pack/prune message areas.

```bash
mutineer-msgpack [options]
  -c, --config <path>
  -a, --area <id>       Target area
  -k, --keep <count>    Messages to retain
  -h, --help
```

### mutineer-userpack

Pack/prune user records.

```bash
mutineer-userpack [options]
  -c, --config <path>
  -h, --help
```

### mutineer-filepack

Pack/prune file area records.

```bash
mutineer-filepack [options]
  -c, --config <path>
  -h, --help
```

### mutineer-netmail-export

Export FidoNet netmail queue.

```bash
mutineer-netmail-export [options]
  -c, --config <path>
  -o, --output <dir>    Output directory
  -h, --help
```

Exports pending records from `fido_netmail` table to BSO format.

### mutineer-validate

Validate menu and template files.

```bash
mutineer-validate <path>

Examples:
  mutineer-validate menus
  mutineer-validate menus/main.mnu
```

Checks parse errors, missing templates, invalid ACS/MCI. Registered as ctest `validate_menus`.

## PLANK Tools

### plankd

PLANK link daemon — manages outbound/inbound sync.

```bash
plankd [options]
  -c, --config <path>
  -d, --database <path>
  -f, --foreground      Don't daemonize
  -h, --help
```

Run alongside `mutineer` for network sync.

### coved

COVE hub daemon for downstream fan-out.

```bash
coved [options]
  -c, --config <path>
  -d, --database <path>
  -h, --help
```

### plankctl

Administrative control for PLANK state.

```bash
plankctl [options] <command> [command-options]

Options:
  -c, --config FILE
  -d, --database FILE
  -V, --version

Commands:
  status        Node status
  init          Initialize node identity
  peers         List peers
  links         List links
  link-add      Add link
  areas         List PLANK areas
  queue         Outbound queue
  journal       Journal entries
  deadletters   Failed deliveries
  quarantine    Quarantined objects
  audit         Audit log
  rescan        Trigger rescan
  requeue       Requeue dead letters
  verify        Integrity check
  export-key    Export public key
```

### plankpack

Bundle pack/unpack for offline exchange.

```bash
plankpack <command> [options]
  export    Create bundle
  import    Import bundle
  -h, --help
```

### plank-offline

Offline import/export operations.

```bash
plank-offline [options] <command> [command-options]
  -c, --config FILE
  -d, --database FILE
  -h, --help
```

## Buccaneer Tools

Built from `src/buccaneer/Makefile` (not CMake root):

| Tool | Purpose |
|------|---------|
| `bucc` | Compile .bucc sources |
| `bucc-linter` | Lint source files |
| `bucc-formatter` | Format source |
| `bucc-simulator` | Run compiled modules |

## Cron Integration

Example scheduler entries (see `scripts/crontabs`):

```cron
0 3 * * * /path/to/mutineer-maint vacuum
0 4 * * * /path/to/mutineer-msgpack -k 500
*/5 * * * * /path/to/plankd -c /etc/mutineer.conf
```

Events in `events` table may also invoke these tools via `scheduler.c`.

## Testing CLI Tools

```bash
ctest --test-dir build -R tools
ctest --test-dir build -R tools_cli
```

`tests/test_tools.c` validates DB operations. `tests/test_tools_cli.sh` runs CLI smoke tests.

## Related Documentation

- [Getting Started](getting-started.md)
- [Sysop Guide](sysop-guide.md)
- [PLANK Networking](networking-plank.md)
- [Configuration](configuration.md)
