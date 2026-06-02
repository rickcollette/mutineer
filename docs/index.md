<!-- generated-by: gsd-doc-writer -->

# Mutineer BBS Documentation

Complete documentation for **Mutineer BBS** — a classic telnet bulletin board system written in C11, styled after Renegade BBS with a green pirate theme. SQLite persistence, modern password hashing, DOSBox DOS door runner, PLANK store-and-forward networking, and the embedded Buccaneer scripting VM.

## Getting Started

| Document | Description |
|----------|-------------|
| [Overview](overview.md) | Project summary, features, layout, binaries, dependencies |
| [Getting Started](getting-started.md) | Install, build, init, run, test, troubleshooting |
| [Configuration](configuration.md) | All `BbsConfig` keys from `include/bbs_config.h` |

## Architecture and Design

| Document | Description |
|----------|-------------|
| [Architecture](architecture.md) | Process model, startup, session lifecycle, components, source map |
| [Menus and UI](menus-and-ui.md) | Menu format, templates, MCI, ACS, art files, F-keys |
| [Developer Guide](developer-guide.md) | Build, test, structure, contributing |

## User-Facing Subsystems

| Document | Description |
|----------|-------------|
| [Messages and Mail](messages-and-mail.md) | Message areas, M*/R* commands, QWK, FidoNet, conferences |
| [Files and Protocols](files-and-protocols.md) | File areas, F* commands, batch, archives, credits |
| [Chat and Social](chat-and-social.md) | Chat modes, paging, wall, whisper, oneliners |
| [Doors and Scripting](doors-and-scripting.md) | Native, DOSBox, Buccaneer status, plugins comparison |

## Sysop and Operations

| Document | Description |
|----------|-------------|
| [Sysop Guide](sysop-guide.md) | WFC, user admin, editors, scheduler, maintenance, security |
| [CLI Tools](cli-tools.md) | All standalone command-line utilities |

## Networking and Extensions

| Document | Description |
|----------|-------------|
| [PLANK Networking](networking-plank.md) | PLANK protocol, daemons, plankctl, schema |
| [Buccaneer](buccaneer.md) | Language, VM, host bridge, toolchain, integration status |
| [Plugins](plugins.md) | Plugin API, building, host API, examples |

## Reference

| Document | Description |
|----------|-------------|
| [Menu Actions](reference/menu-actions.md) | All `handle_action()` actions from `session.c` |
| [Message Commands](reference/message-commands.md) | M* and R* command reference |
| [File Commands](reference/file-commands.md) | F* command reference |
| [ACS and MCI](reference/acs-mci.md) | Full ACS terms and MCI tokens |
| [Database Schema](reference/database.md) | All SQLite tables (BBS + PLANK) |

## Legacy and Supplementary Docs

These older docs remain in the repository for historical context. Prefer the documents above for current reference:

- `docs/admin.md` — earlier admin reference
- `docs/PLANK_ADMIN.md` — earlier PLANK admin guide
- `docs/ACS_MCI.md`, `docs/MCI.md`, `docs/TEMPLATES.md` — earlier template docs
- `SPECS/` — design specifications (Buccaneer, DOS doors, PLANK)

## Quick Links

```bash
# Build and run
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/mutineer-initbbs -c conf/mutineer.conf -y
build/mutineer -c conf/mutineer.conf
```

Default telnet port: **2929** (configurable in `conf/mutineer.conf`).

## License

Mutineer BBS is released under the [MIT License](../LICENSE).
