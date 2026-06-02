<!-- generated-by: gsd-doc-writer -->

# Menu Actions Reference

All actions dispatched by `handle_action()` in `src/session.c`. Menu items specify the action string in the third field of `.mnu` files.

Format: `KEY|Label|Action|Data|ACS`

## General Actions

| Action | Description |
|--------|-------------|
| `who` | List online users and node status |
| `help` | Display basic help text |
| `logout` | End session (`s->alive = 0`) |
| `wall` | Broadcast wall message to all online users |
| `whisper` | Send private message to specific node |
| `lastcallers` | Show recent caller history |

## Message and File

| Action | Description |
|--------|-------------|
| `messages` | Enter message area browser/post flow |
| `files` | Enter file area browser/download flow |
| `fsedit` | Open full-screen editor |
| `setsignature` | Set user message signature |
| `settagline` | Set user tagline |
| `togglefse` | Toggle full-screen editor mode |

## Chat and Social

| Action | Description |
|--------|-------------|
| `chat` | Enter chat subsystem |
| `linechat` | Line-oriented chat mode |
| `splitchat` | Split-screen two-panel chat |
| `page` | Page sysop with message |
| `oneliners` | View/post oneliners |
| `smw` | Short messages waiting |
| `bulletins` | Display sysop bulletins |

## Doors and Plugins

| Action | Description |
|--------|-------------|
| `doors` | List and launch doors |
| `plugins` | List loaded plugins (sysop) |
| `plugin:<id>` | Run plugin by ID (e.g., `plugin:com.mutineer.hello`) |

## User Features

| Action | Description |
|--------|-------------|
| `vote` | Vote booth |
| `voteresults` | View vote results |
| `timebank` | Time bank deposit/withdraw |
| `pickscheme` | Select ANSI color scheme (0–7) |
| `subscribe` | Purchase/activate subscription |
| `setsecurityq` | Set security question/answer |

## Network Mail

| Action | Description |
|--------|-------------|
| `netmail` | QWK/netmail menu |
| `fidosend` | Send pending FidoNet netmail |

## Conferences

| Action | Description |
|--------|-------------|
| `joinconf` | Join a conference |
| `leaveconf` | Leave current conference |
| `conflist` | List conferences |

## File Batch and Archives

| Action | Description |
|--------|-------------|
| `batchrun` | Execute file batch download queue |
| `setfilescandate` | Set new-file scan cutoff date |
| `archivetest` | Test archive integrity |
| `archiveextract` | Extract archive contents |
| `batchremove` | Remove item from batch queue |
| `batchupload` | Batch upload files |

## Sysop Editors

| Action | Description |
|--------|-------------|
| `useredit` | User record editor |
| `areaadmin` | Message area administration |
| `fileadmin` | File area administration |
| `confeditor` | Conference editor |
| `protocoleditor` | Transfer protocol editor |
| `menueditor` | Menu file editor |
| `voteeditor` | Vote topic editor |
| `eventeditor` | Scheduler event editor |
| `fidoeditor` | FidoNet AKA/echolink editor |
| `qwkneteditor` | QWK hub/area link editor |
| `subscriptioneditor` | Subscription type editor |
| `validatefiles` | Validate file areas vs disk |

## Maintenance

| Action | Description |
|--------|-------------|
| `maintenance` | Maintenance menu (pack, stats, rebuild) |

## Navigation

| Action | Description |
|--------|-------------|
| `menu` | Navigate to submenu (data = menu filename) |

Example: `M|Messages|menu|message.mnu`

## Command Passthrough

Some menu items pass M*/R*/F* commands via the data field:

```
Z|Toggle Scan Areas|messages|MZ|L10
```

This invokes message mode with `MZ` command.

## Plugin Action Format

```
plugin:<plugin_id>
```

Examples from `menus/main.mnu`:

- `plugin:com.mutineer.hello`
- `plugin:com.mutineer.chat`

## ACS Requirements

Actions do not enforce ACS themselves — menu items carry ACS in the fourth/fifth field. Additional runtime checks:

- AC restriction flags (e.g., `AC_RMSG` blocks `messages`)
- Sysop actions typically require `+A` AR flag in menu ACS
- Individual editors may check level inside handler

## Adding New Actions

1. Add `else if (!strcmp(action, "myaction"))` branch in `handle_action()`
2. Implement handler logic
3. Add menu item referencing action name
4. Update this reference

Source: `src/session.c` starting at line 1277.

## Related Documentation

- [Menus and UI](../menus-and-ui.md)
- [Sysop Guide](../sysop-guide.md)
- [Plugins](../plugins.md)
