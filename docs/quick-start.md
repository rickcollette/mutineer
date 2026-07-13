# Quick Start

Get Mutineer BBS running in a few minutes. For full build options, Windows, and production deployment, see the links at the end.

## Fastest path: Docker

Works on **Linux, macOS, and Windows** (Docker Desktop).

```bash
git clone https://github.com/rickcollette/mutineer.git
cd mutineer
docker compose up -d
```

Wait until the container is healthy, then connect:

```bash
telnet localhost 2929
```

Or use a BBS client (SyncTERM, mTelnet, etc.) on port **2929**.

**Default sysop account** (created on first container start):

| Field | Value |
|-------|-------|
| Handle | `sysop` |
| Password | `mutineer` |

Change the password after first login via the user editor.

### Useful Docker commands

```bash
docker compose logs -f          # watch logs
docker compose restart          # restart BBS
docker compose down             # stop
docker compose down -v          # stop and wipe database volume
```

Data persists in Docker volumes `mutineer-bbs_mutineer-data` and `mutineer-bbs_mutineer-logs`.

## Linux: pre-built release (no compile)

Download a tarball from [GitHub Releases](https://github.com/rickcollette/mutineer/releases) for your distro, then:

```bash
tar xzf mutineer-*-x86_64-debian.tar.gz
cd mutineer-*-x86_64-debian
./bin/mutineer-initbbs -c conf/mutineer.conf -y
./mutineer -c conf/mutineer.conf
telnet localhost 2929
```

## Linux: build from source

```bash
git clone https://github.com/rickcollette/mutineer.git
cd mutineer
sudo apt-get install -y cmake build-essential libsqlite3-dev libssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/mutineer-initbbs -c conf/mutineer.conf -y
build/mutineer -c conf/mutineer.conf
```

## After you're in

1. Answer **Y** at the ANSI graphics prompt (recommended).
2. Log in as **sysop** / **mutineer** (Docker default).
3. Explore the main menu — messages, files, chat, sysop tools.

## WFC console (sysop)

The detached Docker service runs without a local TTY, so the **Waiting For Caller** screen is off by default. To open WFC interactively:

```bash
./scripts/open-wfc.sh
```

See [Sysop Guide](sysop-guide.md) for WFC keys and operations.

## Next steps

| Topic | Document |
|-------|----------|
| Windows (Docker / WSL) | [Running on Windows](windows.md) |
| Full install & tests | [Getting Started](getting-started.md) |
| Production & Docker details | [Deployment](deployment.md) |
| Configuration keys | [Configuration](configuration.md) |
| Customize menus & art | [Menus and UI](menus-and-ui.md) |
| Buccaneer door language | [Buccaneer Programmer's Guide](buccaneer/programmers-guide.md) |
