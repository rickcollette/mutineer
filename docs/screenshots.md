# Screenshots

## Manual (recommended)

1. **BBS** — with `docker compose up -d` running:

   ```bash
   ./scripts/open-bbs-telnet.sh
   ```

   Answer `Y` for ANSI, log in as **sysop** / **mutineer**, walk menus, and screenshot your terminal.

2. **WFC** — needs an interactive TTY (not the detached compose service):

   ```bash
   ./scripts/open-wfc.sh
   ```

   Screenshot the WFC node grid. Press **Q** to quit.

## Automated PNG capture

With the BBS running (`docker compose up -d`):

```bash
./scripts/capture-screenshots.sh
```

Telnet screens use **expect** + headless Chromium; WFC uses **docker run -it** + the same renderer. Output in `screenshots/`:

| File | Content |
|------|---------|
| `01-motd-login.png` | MOTD, ANSI prompt, handle |
| `02-main-menu.png` | Main deck menu |
| `03-whos-online.png` | Who's online |
| `04-messages.png` | Messages menu |
| `05-wfc-console.png` | WFC node grid (requires image built with `docker/mutineer.wfc.conf`) |

Host packages (Debian/Ubuntu): `xvfb xterm xdotool imagemagick`.

## Manual capture (your terminal)

1. **BBS** — telnet into the running container:

   ```bash
   ./scripts/open-bbs-telnet.sh
   ```

   Log in: `sysop` / `mutineer`, navigate menus, screenshot the terminal.

2. **WFC** — separate interactive container (needs a real TTY):

   ```bash
   ./scripts/open-wfc.sh
   ```

   The WFC screen is the process foreground. Screenshot the terminal window.

The main `docker compose` service keeps `wfc_enabled=0` (no TTY in detached mode). WFC is only started via `open-wfc.sh` or the capture script.
