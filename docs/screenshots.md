# Screenshots

## Manual (recommended)

1. **BBS** — with `docker compose -f docker/compose.yml up -d` running:

   ```bash
   ./scripts/open-bbs-telnet.sh
   ```

   Answer `Y` for ANSI, log in as **sysop** / **mutineer**, walk menus, and screenshot your terminal.

2. **WFC** — the standalone console client needs an interactive TTY:

   ```bash
   ./scripts/open-wfc.sh
   ```

   Screenshot the WFC node grid. Press **Q** to quit.

## Automated PNG capture

With the BBS running (`docker compose -f docker/compose.yml up -d`):

```bash
./scripts/capture-screenshots.sh
```

Telnet screens use **expect** + headless Chromium; WFC uses `mutineer-console`
through `scripts/open-wfc.sh` plus the same renderer. When Chromium is missing,
the renderer still writes checked-in HTML captures. Output in `screenshots/`:

| File | Content |
|------|---------|
| `01-motd-login.png` | MOTD, ANSI prompt, handle |
| `02-main-menu.png` | Main deck menu |
| `03-whos-online.png` | Who's online |
| `04-messages.png` | Messages menu |
| `05-wfc-console.png` | WFC node grid from `mutineer-console` |
| `05-wfc-console.html` | HTML fallback/rendered WFC capture |

Host packages (Debian/Ubuntu): `xvfb xterm xdotool imagemagick`.

## Manual capture (your terminal)

1. **BBS** — telnet into the running container:

   ```bash
   ./scripts/open-bbs-telnet.sh
   ```

   Log in: `sysop` / `mutineer`, navigate menus, screenshot the terminal.

2. **WFC** — standalone console client:

   ```bash
   ./scripts/open-wfc.sh
   ```

   The WFC screen is the process foreground. Screenshot the terminal window.

The main daemon keeps the console-control TCP service enabled by config. The
WFC dashboard itself is only started by `open-wfc.sh` or the capture script.
