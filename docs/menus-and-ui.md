<!-- generated-by: gsd-doc-writer -->

# Menus and UI

Mutineer BBS presents a classic ANSI/ASCII menu interface modeled after Renegade BBS. Menus combine definition files (`.mnu`), display templates (`.ans`/`.asc`), MCI token expansion, and ACS access control.

## Menu Files

Menu definitions live in `menus/` as `.mnu` files. The main menu is configured via `menu_main` in config (default `menus/main.mnu`).

### Enhanced Format

```
# Comment line
@TITLE Main Menu
@FLAGS clrscr,autotime
@PROMPT Command:
@ART main.ans
@FALLBACK logon
@COLS 2
KEY|Label|Action|Data|ACS|CmdFlags|Password
```

### Legacy Format (still supported)

```
KEY|Label|Action|ACS
```

Example from `menus/main.mnu`:

```
W|Who's Online|who|
M|Messages|messages|L10
F|Files|files|L10
O|Doors|doors|L20
Q|Logoff|logout|
```

### Menu Directives

| Directive | Description |
|-----------|-------------|
| `@TITLE` | Menu title displayed in generic renderer |
| `@FLAGS` | Comma-separated menu flags (see below) |
| `@PROMPT` | Custom input prompt (default `Selection: `) |
| `@ART` | ANSI/ASCII art file to display before menu |
| `@FALLBACK` | Fallback menu if no valid selection |
| `@COLS` | Column count for generic menu layout |

### Menu Flags (`include/bbs_menu.h`)

| Flag | Constant | Effect |
|------|----------|--------|
| `clrscr` | `MENU_FLAG_CLRSCR_BEFORE` | Clear screen before display |
| `nocenter` | `MENU_FLAG_DONT_CENTER` | Don't center menu title |
| `notitle` | `MENU_FLAG_NO_MENU_TITLE` | Hide menu title |
| `pause` | `MENU_FLAG_FORCE_PAUSE` | Pause after display |
| `autotime` | `MENU_FLAG_AUTO_TIME` | Show time remaining |
| `noprompt` | `MENU_FLAG_NO_MENU_PROMPT` | Hide selection prompt |
| `lightbar` | `MENU_FLAG_USE_LIGHTBAR` | Lightbar navigation |
| `hotkeys` | `MENU_FLAG_HOTKEYS` | Hot keys (no ENTER required) |

### Command Flags

| Flag | Constant | Effect |
|------|----------|--------|
| `hidden` | `CMD_FLAG_HIDDEN` | Don't display in menu |
| `unhidden` | `CMD_FLAG_UNHIDDEN` | Always display (override ACS hide) |
| `password` | `CMD_FLAG_PASSWORD` | Require password entry |
| `sysoplog` | `CMD_FLAG_SYSOP_LOG` | Log selection to sysop log |

### Menu Item Fields

| Field | Description |
|-------|-------------|
| `KEY` | Single character or multi-char key string |
| `Label` | Display text (may contain MCI tokens) |
| `Action` | Action name passed to `handle_action()` |
| `Data` | Optional parameter (command data, submenu name) |
| `ACS` | Access control expression; empty = everyone |
| `CmdFlags` | Command flags (enhanced format) |
| `Password` | Required password if `password` flag set |

### Submenus

Use action `menu` with data set to another menu filename (without path):

```
R|Return|menu|
```

Or navigate to a named menu:

```
M|Messages|menu|message.mnu
```

## Template Files

Each menu typically has companion art files in the same directory:

| Extension | Format |
|-----------|--------|
| `.ans` | ANSI color art with escape sequences |
| `.asc` | ASCII plain text art |

Templates embed MCI tokens that expand at display time. The `@ART` directive or menu name convention (`main.mnu` → `main.ans`) selects the template.

`menu_template.c` loads the art file, runs `mci_expand()`, and sends the result to the terminal.

## MCI (Menu Code Interface)

MCI tokens embed dynamic data in menus and templates. Implemented in `src/mci.c`.

### Color Scheme Tokens

User-selectable color schemes (0–7) map `^0` through `^9` to ANSI colors:

| Token | Role |
|-------|------|
| `^0` | Primary accent |
| `^1` | Secondary |
| `^2` | Text |
| `^3` | Dim text |
| `^4` | Highlight |
| `^5` | Error/alert |
| `^6` | Success |
| `^7` | Label |
| `^8` | Border |
| `^9` | Title |

Schemes: Mutineer Green (default), Classic Blue, Amber, Cyan, Red Sunset, Magenta, Monochrome, Bright White.

Users change scheme via the `pickscheme` menu action.

### Attribute Tokens

| Token | Effect |
|-------|--------|
| `~L#` | Set foreground color (0–15) |
| `~B#` | Set background color (0–7) |
| `~K0` / `~K1` | Blink off/on |
| `~RS` | Reset all attributes |

### User MCI (`~XX`)

| Token | Expands To |
|-------|------------|
| `~UN` | User handle |
| `~RN` | Real name |
| `~U#` | User ID |
| `~AG` | Age (from birth date) |
| `~BD` | Birth date |
| `~SX` | Sex (M/F/U) |
| `~SL` | Security level |
| `~CR` | Credits |
| `~FP` | File points |
| `~TL` | Time left (minutes) |
| `~TB` | Time bank balance |
| `~LG` | Total logons |
| `~NN` | Node number |
| `~MP` | Messages posted |
| `~UL` / `~DL` | Upload/download counts |
| `~UK` / `~DK` | Upload/download KB |

### System MCI (`~XX`)

| Token | Expands To |
|-------|------------|
| `~BN` | BBS name |
| `~SN` | Sysop name |
| `~TC` | Total calls |
| `~NU` | Number of users |
| `~NF` | Number of files |
| `~NM` | Number of messages |
| `~NO` | Users online |
| `~VR` | Version string |
| `~DA` | Current date |
| `~TM` | Current time |

### Area MCI

| Token | Expands To |
|-------|------------|
| `~AN` | Current message or file area name |
| `~AR` | User AR flags list |

### Legacy Percent Codes (`%XX`)

| Token | Expands To |
|-------|------------|
| `%UN` | User handle |
| `%SL` | Security level |
| `%CR` | Credits |
| `%TL` | Time left |
| `%NN` | Node number |
| `%DA` / `%TM` | Date/time |
| `%PO` | Posts count |
| `%MT` / `%FT` | Total messages/files |
| `%MA` / `%FA` | Messages/files in current area |
| `%NO` | Users online |
| `%PE` | Pause prompt |
| `%?expr{text\|else}` | ACS conditional text |

Full token list: [ACS and MCI Reference](reference/acs-mci.md).

## ACS (Access Control Strings)

ACS expressions control menu item visibility and area access. Evaluated by `acs_allows()` in `src/acs.c`.

### Syntax

- **Precedence:** `!` (NOT) > `&` (AND) > `|` (OR)
- **Grouping:** Parentheses `(expr)`
- **Terms:** Letter + optional digits/character

### Standard Terms

| Term | Meaning |
|------|---------|
| `S#` | Security level >= # |
| `D#` | Download security level >= # |
| `F?` | AR flag ? set (A–Z) |
| `R?` | AC restriction flag ? set |
| `A#` | Age >= # |
| `C#` | Total logons >= # |
| `C?` | In conference ? (A–Z) |
| `G?` | Gender is ? (M/F) |
| `H#` | Current hour = # |
| `N#` | Node number = # |
| `P#` | Credits >= # |
| `T#` | Time remaining >= # |
| `U#` | User number = # |
| `V` | User validated (not expired) |
| `W#` | Day of week = # (0=Sun) |
| `X#` | Days until expiration <= # |
| `Z` | Meets post ratio |
| `PC` | Post/call ratio met |
| `DR` | Download ratio met |
| `J#` | Member of conference # |
| `E#` | Active subscription type # (0=any) |

### Legacy Terms

| Term | Meaning |
|------|---------|
| `SLnn` / `Lnn` | Security level >= nn |
| `+X` | AR flag X set |
| `ARABC` | All listed AR flags set |
| `C>=N` | Credits comparison |
| `T>=N` | Time comparison |
| `R>=N` | Ratio comparison |

### Menu ACS Examples

```
L10          # Level 10 or higher
+A           # AR flag A (sysop)
S50&FA       # Level 50+ with AR flag F
L20|+B       # Level 20+ OR AR flag B
!(R P)       # NOT restricted from posting
```

## Art Files

System art lives in `art/` (configurable via `art_path`):

| File | Typical Use |
|------|-------------|
| `motd.ans` | Message of the day (config `motd`) |
| `welcome.txt` | Welcome letter text |
| `logon.ans` | Logon screen |
| Named art | Referenced by `send_named_art()` (e.g., `NOCREDTS`) |

Art files are plain files with ANSI escape sequences. They may contain MCI tokens expanded at display time.

Validate art paths during development with `mutineer-validate`.

## F-Key Shortcuts

Sysop remote commands are bound to function keys during online sessions. The session loop intercepts F-key ANSI sequences and dispatches to `handle_action()` or editors.

Typical bindings (sysop level required):

| Key | Action |
|-----|--------|
| F1 | User editor |
| F2 | Conference editor |
| F3 | Protocol editor |
| F4 | Menu editor |
| F5 | Validate files |
| F6 | Vote editor |
| F7 | Event editor |
| F8 | Fido editor |
| F9 | QWK network editor |
| F10 | Maintenance |

Exact bindings are defined in `session.c` F-key handling. Sysop must have appropriate AR flags (typically `+A`).

## Menu Validation

Validate all menus before deployment:

```bash
build/mutineer-validate menus
build/mutineer-validate menus/main.mnu
```

Checks: file parse, ACS syntax, template existence, MCI token validity.

## Related Documentation

- [Reference: ACS and MCI](reference/acs-mci.md)
- [Reference: Menu Actions](reference/menu-actions.md)
- [Architecture](architecture.md)
- [Sysop Guide](sysop-guide.md) — menu editor
