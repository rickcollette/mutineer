<!-- generated-by: gsd-doc-writer -->

# Getting Started

This guide walks from a fresh clone to a running Mutineer BBS with passing tests.

**Just want it running fast?** See [Quick Start](quick-start.md). **On Windows?** See [Running on Windows](windows.md). **Production Docker/releases:** [Deployment](deployment.md).

## Prerequisites

| Requirement | Version | Notes |
|-------------|---------|-------|
| Linux | Any recent distro | Primary target platform |
| CMake | >= 3.16 | Build system |
| GCC or Clang | C11 support | `-std=c11` required |
| SQLite3 dev | 3.x | `libsqlite3-dev` |
| OpenSSL dev | 1.1+ or 3.x | `libssl-dev` |
| pthreads | — | Provided by libc on Linux |
| make / ninja | — | CMake generator backend |

Optional but recommended:

| Package | Purpose |
|---------|---------|
| `libargon2-dev` | Argon2 password upgrade path |
| `dosbox` | DOS door runner |
| `expect` | Interactive integration tests |
| `libzstd-dev` | PLANK bundle compression |

## Pre-built releases

GitHub Releases provide x86_64 tarballs — no compile step required:

| Download | Target systems |
|----------|----------------|
| `*-x86_64-debian.tar.gz` | **Debian 12+** and **Ubuntu 24.04 LTS+** |
| `*-x86_64-fedora.tar.gz` | **Fedora 39+** |
| `*-x86_64-alpine.tar.gz` | **Alpine 3.18+** |

Download from [GitHub Releases](https://github.com/rickcollette/mutineer/releases).

```bash
tar xzf mutineer-v1.0.0-x86_64-debian.tar.gz
cd mutineer-v1.0.0-x86_64-debian
./bin/mutineer-initbbs -c conf/mutineer.conf -y
./mutineer -c conf/mutineer.conf
telnet localhost 2929
```

Each archive includes the daemon, CLI tools, PLANK tools, sample plugins, default config/menus/art, and `INSTALL.md` with runtime dependencies. SHA256 checksums are published alongside each tarball.

To cut a release as a maintainer: tag `v1.0.0` and push — GitHub Actions builds all three platforms and publishes assets automatically.

## Installation Steps

### 1. Clone the repository

```bash
git clone <repository-url> mutineer-bbs
cd mutineer-bbs
```

### 2. Install dependencies

Debian/Ubuntu:

```bash
sudo apt-get install cmake build-essential libsqlite3-dev libssl-dev \
  libargon2-dev dosbox expect
```

### 3. Configure and build

Release build (production):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Debug build (development with tests):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Build only CLI tools:

```bash
cmake --build build --target tools
```

Build PLANK tools:

```bash
cmake --build build --target plank
```

Build sample plugins:

```bash
cmake --build build --target plugins
```

### 4. Initialize the BBS

Run the initialization utility to create directories, apply the SQLite schema, and seed default data:

```bash
build/mutineer-initbbs -c conf/mutineer.conf -y
```

Interactive mode (prompts for each missing item):

```bash
build/mutineer-initbbs -c conf/mutineer.conf
```

Dry run (report only):

```bash
build/mutineer-initbbs -c conf/mutineer.conf -n
```

The init tool creates:

- `data/mutineer.db` — SQLite database with schema from `sql/schema.sql`
- Required directories (`data/`, `logs/`, file area paths, etc.)
- Default security levels, sysop account prompt, sample areas

### 5. First run

Start the BBS server:

```bash
build/mutineer -c conf/mutineer.conf
```

Expected output:

```
mutineer listening on 0.0.0.0:2929
```

Connect via telnet:

```bash
telnet localhost 2929
```

Or use a BBS terminal client (SyncTERM, mTelnet, etc.) pointed at port **2929**.

### 6. Run tests

Unit and integration tests (excludes slow interactive expect suite):

```bash
ctest --test-dir build --exclude-regex "tools_cli|expect_suite"
```

Run a specific test binary:

```bash
build/test_acs
build/test_doors
build/test_plank_store
```

Validate all menu files:

```bash
build/mutineer-validate menus
```

Run interactive expect tests (requires `expect`):

```bash
ctest --test-dir build -R expect_suite
```

## Post-Install Checklist

1. Edit `conf/mutineer.conf` — set `bbs_name`, `sysop_name`, paths, limits.
2. Customize `menus/main.mnu` and associated `.ans`/`.asc` templates.
3. Add art files under `art/` (MOTD, welcome, error screens).
4. Create message and file areas via sysop editors or direct DB seeding.
5. Configure doors in the `doors` table and place door files under `doors/`.
6. Set up cron/scheduler events for maintenance (`mutineer-maint`, pack tools).

## Shell Scripts

Helper scripts in `scripts/`:

| Script | Purpose |
|--------|---------|
| `scripts/start` | Start BBS in background |
| `scripts/stop` | Stop running BBS |
| `scripts/start-screen` | Start in GNU screen session |
| `scripts/backup` | Database backup helper |
| `scripts/watchdog` | Restart on failure |
| `scripts/bbs-wall` | Send wall message to online users |

## Common Setup Issues

### Database open failed

**Symptom:** `DB open failed: data/mutineer.db` on startup.

**Fix:** Run initialization:

```bash
build/mutineer-initbbs -c conf/mutineer.conf -y
```

### Startup sanity checks failed

**Symptom:** `Startup sanity checks failed. Cannot start BBS with missing required files.`

**Fix:** Init creates missing paths. Verify `menu_main`, `motd`, and `art_path` files exist. Check `conf/mutineer.conf` paths match your layout.

### Port already in use

**Symptom:** Listener fails to bind port 2929.

**Fix:** Change `port=` in config or stop the conflicting process:

```bash
ss -tlnp | grep 2929
```

### Missing SQLite or OpenSSL at link time

**Symptom:** CMake `find_package` errors.

**Fix:** Install dev packages:

```bash
sudo apt-get install libsqlite3-dev libssl-dev
```

### Login throttled during testing

**Symptom:** Repeated failed logins blocked.

**Fix:** Relax limits in config for development:

```ini
login_window_sec=10
login_max_attempts=100
```

The default `conf/mutineer.conf` already uses relaxed values for testing.

### Plugins not loading

**Symptom:** `Plugin not found` or `Plugins are not enabled`.

**Fix:** Ensure plugins are built and config enables them:

```ini
plugins_enabled=true
plugins_dir=plugins
```

Plugin `.so` files must be in `build/plugins/` or the configured directory. Build with `cmake --build build --target plugins`.

### DOS doors fail to launch

**Symptom:** Door exits immediately or DOSBox not found.

**Fix:** Install DOSBox and verify path:

```ini
dosbox_path=dosbox
door_runtime_path=data/door_runtime
```

Check door manifest JSON and `doors` DB record. See [Doors and Scripting](doors-and-scripting.md).

## Docker

```bash
docker compose up -d
telnet localhost 2929
```

Default sysop: **sysop** / **mutineer**. See [Deployment](deployment.md) and [Windows](windows.md).

## Next Steps

- [Quick Start](quick-start.md) — minimal path
- [Configuration](configuration.md) — all config keys
- [Sysop Guide](sysop-guide.md) — WFC, user admin, maintenance
- [Menus and UI](menus-and-ui.md) — customize menus and templates
- [Buccaneer Programmer's Guide](buccaneer/programmers-guide.md) — door language
- [Developer Guide](developer-guide.md) — code structure and contributing
- [CLI Tools](cli-tools.md) — maintenance utilities
