# Running Mutineer on Windows

Mutineer is a **Linux-native** C program. On Windows you run it through **Docker Desktop** (easiest) or **WSL2** (full Linux toolchain). Native Windows builds are not supported today.

## Option A: Docker Desktop (recommended)

Best if you want a running BBS with minimal setup and no compiler install.

### Prerequisites

1. Install [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/).
2. Enable **WSL 2 backend** when prompted (Docker Desktop uses it internally).
3. Install a telnet client:
   - **Windows 11**: `Optional Features` → enable **Telnet Client**, or
   - Use **SyncTERM**, **PuTTY** (raw telnet to `localhost:2929`), or **Windows Terminal** + `telnet localhost 2929`.

### Run the BBS

Open **PowerShell** or **Command Prompt** in the project folder:

```powershell
git clone https://github.com/rickcollette/mutineer.git
cd mutineer
docker compose up -d
```

Connect:

```powershell
telnet localhost 2929
```

Default login: **sysop** / **mutineer**.

### Port and firewall

Docker publishes port **2929** to the host. If connection fails:

- Confirm the container is running: `docker compose ps`
- Check Windows Firewall allows Docker Desktop
- Try `127.0.0.1` instead of `localhost` in your client

### Persisted data

User database and uploads live in Docker volumes. To reset completely:

```powershell
docker compose down -v
docker compose up -d
```

### Screenshots / WFC

WFC needs an interactive terminal. From WSL or Git Bash inside the repo:

```bash
./scripts/open-wfc.sh
./scripts/open-bbs-telnet.sh
```

See [Screenshots](screenshots.md).

---

## Option B: WSL2 (Ubuntu)

Best if you want to **build from source**, run tests, or develop on Windows.

### Prerequisites

1. Install WSL2: in PowerShell (Admin):

   ```powershell
   wsl --install -d Ubuntu
   ```

2. Reboot, open **Ubuntu** from the Start menu, create your Linux user.

3. Inside WSL, install build dependencies:

   ```bash
   sudo apt update
   sudo apt install -y git cmake build-essential libsqlite3-dev libssl-dev \
     telnet dosbox expect
   ```

### Clone and build

```bash
cd ~
git clone https://github.com/rickcollette/mutineer.git
cd mutineer
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/mutineer-initbbs -c conf/mutineer.conf -y
build/mutineer -c conf/mutineer.conf
```

In a **second WSL terminal**:

```bash
telnet localhost 2929
```

### Access from Windows clients

WSL2 forwards `localhost` automatically in recent Windows versions. Point SyncTERM or PuTTY at:

- Host: `localhost`
- Port: `2929`

If that fails, get the WSL IP from inside Ubuntu:

```bash
hostname -I | awk '{print $1}'
```

Use that IP in your Windows BBS client instead of `localhost`.

### Docker inside WSL

You can also run `docker compose up -d` from the repo in WSL if Docker Desktop integrates with WSL (Settings → Resources → WSL Integration → enable your distro).

---

## Option comparison

| Approach | Build from source | Telnet from Windows | Sysop WFC | Difficulty |
|----------|-------------------|---------------------|-----------|------------|
| Docker Desktop | No | Yes | Via WSL script | Low |
| WSL2 Ubuntu | Yes | Yes | Yes in WSL terminal | Medium |
| Native Windows | Not supported | — | — | — |

---

## Troubleshooting

### `telnet` is not recognized (PowerShell)

Enable Telnet Client in Windows Features, install SyncTERM, or use WSL: `wsl telnet localhost 2929`.

### Docker: `docker compose` not found

Use Docker Desktop’s built-in Compose v2: `docker compose` (with a space). Update Docker Desktop if the command is missing.

### WSL: cannot connect to port 2929 from Windows

Restart WSL: `wsl --shutdown`, then start Ubuntu again. Confirm `mutineer` is listening: `ss -tlnp | grep 2929` inside WSL.

### Login fails after many test attempts

Reset the database volume (Docker) or re-run `mutineer-initbbs -y` (WSL). Default sysop password is **mutineer** on a fresh install.

---

## Next steps

- [Quick Start](quick-start.md)
- [Getting Started](getting-started.md) — tests, plugins, doors
- [Deployment](deployment.md) — Docker volumes, releases, production
