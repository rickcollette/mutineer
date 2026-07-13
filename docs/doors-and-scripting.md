<!-- generated-by: gsd-doc-writer -->

# Doors and Scripting

Mutineer BBS supports four extension mechanisms for interactive programs: native executable doors, DOSBox-hosted DOS doors, Buccaneer scripts, and loadable `.so` plugins.

## Door Types Comparison

| Feature | Native Door | DOSBox Door | Buccaneer Script | Plugin (.so) |
|---------|-------------|-------------|------------------|--------------|
| Language | Any executable | DOS (16/32-bit) | Buccaneer (.bucc) | C |
| Isolation | fork/exec | DOSBox VM + runtime dir | In-process VM | In-process dlopen |
| Dropfiles | Yes | Yes | Via host bridge | Via host API |
| Serial I/O | Socket inherit | Nullmodem socket | Terminal I/O | Terminal I/O |
| Hot reload | No | No | Recompile | Reload .so |
| Best for | Linux ports | Classic DOS doors | New interactive scripts | Complex native logic |

## Native Doors

Native doors are executables launched via `fork`/`exec` from `src/doors.c`.

Native door commands are argv templates, not shell scripts. Mutineer parses quoted arguments and backslash escapes, then launches the child directly with `execvp()`. Shell metacharacters such as pipes, redirects, command substitution, `;`, `&&`, and `||` are rejected.

### Database Record (`doors` table)

| Field | Purpose |
|-------|---------|
| `name` | Door display name |
| `command` | Executable argv template |
| `dropfile` | Dropfile format (DOOR.SYS, DORINFO1.DEF) |
| `workdir` | Working directory |
| `acs` | Access control |
| `flags` | Door flags |

### Launch Flow

1. User selects door from `doors` menu action
2. ACS check on door record
3. Dropfile written to `dropfile_path` with session/user/node info
4. Child process spawned with inherited telnet socket FD
5. Parent waits for exit or timeout (`door_default_timeout_sec`)
6. Session resumes at BBS menu

## DOSBox Doors

Classic DOS BBS doors run under DOSBox with per-launch isolation.

### Configuration

```ini
dosbox_path=dosbox
door_runtime_path=data/door_runtime
door_copy_mode=copy
door_default_timeout_sec=300
door_cleanup_on_exit=1
door_keep_failed_runs=0
```

### Door Manifest (`door.json`)

Flat JSON in door directory (see `doors/testdoor/testdoor.json`):

```json
{
  "name": "Test Door",
  "runner": "dosbox",
  "executable": "TESTDOOR.COM",
  "dropfile": "DOOR.SYS",
  "timeout_sec": 300
}
```

### Launch Flow

1. Create isolated runtime tree under `door_runtime_path/<uuid>/`
2. Copy door files per `door_copy_mode`
3. Generate DOSBox config with serial nullmodem on inherited socket
4. Launch DOSBox; DOS door receives COM port via nullmodem
5. On exit: cleanup runtime tree (unless failure + `door_keep_failed_runs`)

### Included Examples

| Path | Description |
|------|-------------|
| `doors/testdoor/` | TESTDOOR.COM — real 16-bit DOS COM |
| `SPECS/DOSDOORS/aladdin/` | Aladdin Adventure (1992) with manifest |

## Buccaneer Scripting

Buccaneer is Mutineer's interpreted language for BBS addons, games, and extensions. Source modules compile to bytecode and execute in-process through the Buccaneer runtime in `src/buccaneer/`.

### Toolchain

| Tool | Purpose |
|------|---------|
| `bucc` | Compiler (.bucc → bytecode module) |
| `bucc-linter` | Static analysis |
| `bucc-formatter` | Source formatting |
| `bucc-simulator` | Standalone VM testing |

Build with the top-level CMake project. The standalone `src/buccaneer/Makefile` remains useful for focused language work.

### Example Scripts

| File | Description |
|------|-------------|
| `examples/hello.bucc` | Hello world |
| `examples/guess.bucc` | Number guessing game |
| `examples/scoreboard.bucc` | Score tracking demo |

### BBS Launch

Door records can use `runner=bucc`. The door's `manifest` field is treated as the Buccaneer door manifest path, and the BBS binds session, terminal, user, and BBS host APIs before running the package. Manifest capabilities are enforced at host dispatch time.

Known remaining Buccaneer runtime work:

- `DOOR.CHAIN` sets wrong VM status (should be `VM_CHAIN`)
- `DOOR.EXIT` discards exit code
- `OP_DISPATCH_CALL` emits incorrect proc IDs
- Several VM edge cases (array allocation, stack consistency)

Use `bucc-simulator` during development, then register the package as a `runner=bucc` door for BBS launch testing.

See the [Buccaneer Programmer's Guide](buccaneer/programmers-guide.md) and [Buccaneer hub](buccaneer/index.md).

## Plugins

Loadable shared objects providing interactive experiences without modifying core BBS.

### Configuration

```ini
plugins_enabled=true
plugins_dir=plugins
plugins_allowlist=
plugins_denylist=
```

### Menu Integration

```
1|Hello Plugin|plugin:com.mutineer.hello|L10
2|Chat Plugin|plugin:com.mutineer.chat|L10
3|List Plugins|plugins|+A
```

Action format: `plugin:<plugin_id>` where ID matches `bbs_plugin_desc.id`.

### Launch Flow

1. `handle_action()` detects `plugin:` prefix
2. `plugin_registry_find()` locates loaded plugin
3. `create_instance()` creates per-session instance
4. `on_enter()` → `run()` loop → `on_exit()` → `destroy()`

See [Plugins](plugins.md) for API details.

## Dropfiles

Dropfiles pass session context to doors:

| Format | Use |
|--------|-----|
| DOOR.SYS | Wildcat/TradeWars standard |
| DORINFO1.DEF | PCBoard format |

Written to `dropfile_path` before door launch with:

- User handle, level, credits, time left
- Node number, COM port (socket FD mapping)
- BBS name, sysop name

## Door Menu

`menus/door.mnu` lists available doors. The `doors` action in session.c reads from `doors` table and launches selected door.

## Security Considerations

- Native doors run as the BBS user — trust door binaries
- DOSBox isolation limits filesystem exposure to runtime tree
- Path traversal blocked in door manifest parsing (`path_is_safe_relative()`)
- Timeout prevents hung doors from blocking nodes indefinitely
- ACS on door records controls access

## Testing

```bash
build/test_doors
ctest --test-dir build -R doors
```

## Related Documentation

- [Buccaneer Programmer's Guide](buccaneer/programmers-guide.md)
- [Plugins](plugins.md)
- [Configuration](configuration.md) — DOSBox settings
- [Sysop Guide](sysop-guide.md) — door administration
