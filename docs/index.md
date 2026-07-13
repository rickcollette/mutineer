# Mutineer BBS Documentation

Complete documentation for **Mutineer BBS** — a classic telnet bulletin board system in C11, with SQLite, DOS doors, PLANK networking, plugins, and the **Buccaneer** door language.

**Live site:** [rickcollette.github.io/mutineer](https://rickcollette.github.io/mutineer/)

---

## Start here

| Document | Description |
|----------|-------------|
| [Quick Start](quick-start.md) | Run in 5 minutes — Docker or binary |
| [Running on Windows](windows.md) | Docker Desktop and WSL2 |
| [Getting Started](getting-started.md) | Full install, build, test, troubleshooting |
| [Deployment](deployment.md) | Docker, releases, systemd, backups |

---

## BBS operator guides

Deep guides for each major subsystem:

| Document | Topics |
|----------|--------|
| [Overview](overview.md) | Features, layout, binaries, design goals |
| [Configuration](configuration.md) | All `BbsConfig` keys |
| [Menus and UI](menus-and-ui.md) | `.mnu` files, templates, MCI, ACS, art, F-keys |
| [Messages and Mail](messages-and-mail.md) | Areas, M*/R* commands, QWK, FidoNet, conferences |
| [Files and Protocols](files-and-protocols.md) | File areas, batch, archives, credits, protocols |
| [Chat and Social](chat-and-social.md) | Chat, paging, wall, whisper, oneliners |
| [Doors and Scripting](doors-and-scripting.md) | Native, DOSBox, Buccaneer, plugins |
| [Sysop Guide](sysop-guide.md) | WFC, editors, scheduler, maintenance, security |
| [PLANK Networking](networking-plank.md) | Store-and-forward, daemons, plankctl |
| [Plugins](plugins.md) | `.so` plugins, API, examples |
| [CLI Tools](cli-tools.md) | `mutineer-maint`, QWK tools, stats, initbbs |
| [Screenshots](screenshots.md) | Capturing BBS and WFC screens |

---

## Buccaneer (door language)

| Document | Audience |
|----------|----------|
| [Buccaneer hub](buccaneer/index.md) | Index and spec links |
| [**Programmer's Guide**](buccaneer/programmers-guide.md) | **Full language tutorial for door authors** |
| [Host API Reference](buccaneer/host-api.md) | `TERM`, `USER`, `DATA`, `KV`, … |
| [Toolchain](buccaneer/toolchain.md) | `bucc`, linter, formatter, simulator |
| [Door Packages](buccaneer/door-packages.md) | `door.json`, capabilities, install |

---

## Architecture and development

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | Processes, sessions, components, source map |
| [Developer Guide](developer-guide.md) | Contributing, tests, code layout |

---

## Reference

| Document | Description |
|----------|-------------|
| [Menu Actions](reference/menu-actions.md) | `handle_action()` codes |
| [Message Commands](reference/message-commands.md) | M* and R* commands |
| [File Commands](reference/file-commands.md) | F* commands |
| [ACS and MCI](reference/acs-mci.md) | Access control and template tokens |
| [Database Schema](reference/database.md) | SQLite tables |

---

## Legacy docs

Older files kept for history — prefer the guides above:

| File | Superseded by |
|------|----------------|
| `admin.md` | [Sysop Guide](sysop-guide.md) |
| `PLANK_ADMIN.md` | [PLANK Networking](networking-plank.md) |
| `ACS_MCI.md`, `MCI.md`, `TEMPLATES.md` | [Menus and UI](menus-and-ui.md), [ACS reference](reference/acs-mci.md) |
| `networking.md`, `doors_protocols.md` | [PLANK](networking-plank.md), [Doors](doors-and-scripting.md) |
| `SPEC.md`, `FILES.md`, `PARITY.md`, `WHY.md` | [Overview](overview.md), subsystem guides |

Implementer specifications: `SPECS/BUCCANEER/`, `SPECS/` (PLANK, DOS doors).

---

## Quick commands

```bash
# Docker
docker compose up -d && telnet localhost 2929

# From source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
build/mutineer-initbbs -c conf/mutineer.conf -y
build/mutineer -c conf/mutineer.conf
```

Default port: **2929** · Docker default login: **sysop** / **mutineer**

## License

[MIT License](../LICENSE)
