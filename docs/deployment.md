# Deployment

How to run Mutineer in production-like environments: Docker, release tarballs, and host services.

## Docker Compose (recommended for small installs)

The repo includes `Dockerfile` and `docker-compose.yml`.

```bash
docker compose up -d --build
```

| Setting | Default |
|---------|---------|
| Published port | `2929` |
| Config | `conf/mutineer.docker.conf` |
| Data volume | `mutineer-bbs_mutineer-data` → `/opt/mutineer/data` |
| Logs volume | `mutineer-bbs_mutineer-logs` → `/opt/mutineer/logs` |

First start runs `mutineer-initbbs -y` if `data/mutineer.db` is missing.

### Environment

| Variable | Purpose |
|----------|---------|
| `MUTINEER_CONFIG` | Config file path inside container (default `conf/mutineer.docker.conf`) |

### WFC in Docker

The default Docker config sets `wfc_enabled=0` because detached containers have no TTY. Use `scripts/open-wfc.sh` for an interactive WFC session on port 2930, or run the daemon on the host with a real terminal.

### Health check

The image healthcheck probes TCP `127.0.0.1:2929` inside the container.

## GitHub release tarballs

CI builds three x86_64 archives per tag (Debian, Fedora, Alpine). Each contains:

- `mutineer` daemon and `bin/*` tools
- Default `conf/`, `menus/`, `art/`
- `INSTALL.md` with runtime library requirements

Extract, run `mutineer-initbbs -y`, then start `mutineer`. See [Getting Started](getting-started.md).

## Running on a Linux host

### systemd example

```ini
[Unit]
Description=Mutineer BBS
After=network.target

[Service]
Type=simple
User=mutineer
WorkingDirectory=/opt/mutineer
ExecStart=/opt/mutineer/mutineer -c /opt/mutineer/conf/mutineer.conf
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

### Helper scripts

| Script | Purpose |
|--------|---------|
| `scripts/start` | Background start |
| `scripts/stop` | Stop daemon |
| `scripts/start-screen` | GNU screen session |
| `scripts/watchdog` | Restart on failure |
| `scripts/backup` | Database backup |

### Firewall

Open the configured `port` (default **2929/tcp**). Use a reverse proxy only if you understand telnet protocol limitations — BBS clients expect raw telnet, not HTTP.

## Backups

- Primary state: `data/mutineer.db` (SQLite)
- Door runtime: `data/door_runtime/`, uploads under file area paths
- Logs: `logs/mutineer.log`

Schedule `scripts/backup` or snapshot the data directory while the BBS is stopped or via SQLite backup API (`mutineer-maint`).

## Security checklist

1. Change default sysop password immediately.
2. Tighten `login_max_attempts` / `login_window_sec` for production.
3. Restrict `bind=` if the BBS should not listen on all interfaces.
4. Review ACS on message/file areas and sysop menu entries.
5. Audit door and plugin permissions; Buccaneer doors use capability declarations in `door.json` (enforcement improving — see GitHub issues tagged `buccaneer`).

## Related docs

- [Quick Start](quick-start.md)
- [Windows](windows.md)
- [Configuration](configuration.md)
- [Sysop Guide](sysop-guide.md)
