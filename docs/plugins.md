<!-- generated-by: gsd-doc-writer -->

# Plugins

Mutineer BBS supports loadable shared object plugins (`.so`) that provide interactive door-like experiences. Plugins are loaded via `dlopen()` at startup and invoked through menu actions.

## Architecture

```
mutineer startup
    → plugin_loader_init() reads config
    → dlopen each .so in plugins_dir
    → bbs_plugin_query() obtains descriptor
    → plugin_registry stores loaded plugins

User selects menu item: plugin:com.mutineer.hello
    → plugin_registry_find()
    → create_instance() per session
    → on_enter() → run() → on_exit() → destroy()
```

Sources: `plugin_loader.c`, `plugin_registry.c`, `plugin_host_api.c`.

## Configuration

```ini
plugins_enabled=true
plugins_dir=plugins
plugins_allowlist=
plugins_denylist=
```

- `plugins_allowlist` — if set, only listed IDs load
- `plugins_denylist` — excluded IDs even if otherwise allowed
- Built plugins output to `build/plugins/` by default

Build sample plugins:

```bash
cmake --build build --target plugins
# Produces: build/plugins/hello.so, build/plugins/chat_plugin.so
```

## Plugin ABI

Stable ABI defined in `include/bbs_plugin_api.h`:

| Constant | Value |
|----------|-------|
| `BBS_PLUGIN_ABI_VERSION` | `0x00010000` (v1.0) |
| `BBS_PLUGIN_MAGIC` | `0x4250534F` ('BPSO') |

### Required Export

Every plugin must export:

```c
bbs_rc_t bbs_plugin_query(uint32_t host_abi_version,
                          const bbs_host_api_t* host,
                          bbs_plugin_desc_t* out_desc);
```

Define `BBS_PLUGIN_IMPL=1` when compiling the plugin.

## Plugin Descriptor

```c
typedef struct bbs_plugin_desc {
  uint32_t abi_version;
  const char* id;          // "com.mutineer.hello"
  const char* name;
  const char* version;
  const char* author;
  const char* description;
  uint32_t caps;           // BBS_CAP_* flags

  bbs_rc_t (*init)(const bbs_host_api_t* host);
  void     (*shutdown)(void);
  bbs_rc_t (*create_instance)(bbs_session_t* s, void** out_inst,
                              const bbs_plugin_instance_vtbl_t** out_vtbl);
  const bbs_command_def_t* (*commands)(size_t* out_count);
  void (*on_event)(const bbs_event_t* ev);
} bbs_plugin_desc_t;
```

### Capabilities

| Flag | Meaning |
|------|---------|
| `BBS_CAP_INTERACTIVE` | Interactive session plugin |
| `BBS_CAP_COMMANDS` | Registers slash commands |
| `BBS_CAP_BACKGROUND` | Background task capable |

## Instance Vtable

Per-session plugin instance:

```c
typedef struct bbs_plugin_instance_vtbl {
  bbs_rc_t (*on_enter)(void* inst, bbs_session_t* s);
  bbs_rc_t (*run)(void* inst, bbs_session_t* s);
  void     (*on_exit)(void* inst, bbs_session_t* s);
  void     (*on_event)(void* inst, const bbs_event_t* ev);
  void     (*destroy)(void* inst);
} bbs_plugin_instance_vtbl_t;
```

## Host API

Host provides services via `bbs_host_api_t`:

### Logging

```c
void (*log)(bbs_log_level_t lvl, const char* subsystem, const char* msg);
```

Levels: DEBUG (10), INFO (20), WARN (30), ERROR (40).

### Terminal I/O (`bbs_io_t`)

| Method | Purpose |
|--------|---------|
| `write()` | Raw write to terminal |
| `printf()` | Formatted output |
| `readline()` | Read line with echo control |
| `cls()` | Clear screen |

### Scheduler (`bbs_sched_t`)

| Method | Purpose |
|--------|---------|
| `enqueue()` | Run task on worker thread |
| `after_ms()` | Delayed task |
| `cancel()` | Cancel scheduled task |

### Session Info

| Method | Returns |
|--------|---------|
| `session_username()` | User handle |
| `session_user_id()` | Numeric user ID |
| `session_user_flags()` | AR flags |
| `session_remote_addr()` | Client IP |

### Key-Value Store

```c
bbs_rc_t (*kv_get)(const char* ns, const char* key, char* out, size_t out_sz);
bbs_rc_t (*kv_set)(const char* ns, const char* key, const char* val);
```

Namespace-scoped persistence (host-defined backing).

### Plugin Data Directory

```c
bbs_rc_t (*plugin_data_dir)(const char* plugin_id, char* out, size_t out_sz);
```

## Events

Global and per-session events:

| Event | Type |
|-------|------|
| Login | `BBS_EVT_LOGIN` |
| Logout | `BBS_EVT_LOGOUT` |
| Tick | `BBS_EVT_TICK_1S` |
| Shutdown | `BBS_EVT_SHUTDOWN` |

## Building a Plugin

CMake helper in root `CMakeLists.txt`:

```cmake
add_mutineer_plugin(hello hello.c hello)
# → plugins/hello/hello.c → build/plugins/hello.so
```

Manual build:

```bash
gcc -shared -fPIC -DBBS_PLUGIN_IMPL=1 \
  -I include -o myplugin.so myplugin.c
```

## Example: Hello Plugin

`plugins/hello/hello.c` — minimal interactive plugin:

1. Greets user with handle, ID, IP
2. Echo loop until user types `exit`
3. Supports `cls`, `help`, `count` commands

Menu entry in `menus/main.mnu`:

```
1|Hello Plugin|plugin:com.mutineer.hello|L10
```

Plugin ID: `com.mutineer.hello`.

## Example: Chat Plugin

`plugins/chat/chat.c` — plugin-based chat room as alternative to built-in `chat.c`.

Menu entry:

```
2|Chat Plugin|plugin:com.mutineer.chat|L10
```

## Menu Integration

Action format: `plugin:<plugin_id>`

Handled in `handle_action()` (`session.c`):

1. Check `plugin_loader_enabled()`
2. Find plugin in registry
3. Create instance, run lifecycle
4. Return to menu on exit

List loaded plugins: `plugins` action (sysop).

## Return Codes

| Code | Meaning |
|------|---------|
| `BBS_OK` | Success |
| `BBS_EINVAL` | Invalid argument |
| `BBS_EUNSUPPORTED` | Unsupported operation |
| `BBS_EIO` | I/O error (disconnect) |
| `BBS_EPERM` | Permission denied |
| `BBS_EINTERNAL` | Internal error |
| `BBS_ETIMEOUT` | Timeout |

## Testing

```bash
cmake --build build --target plugins
ctest --test-dir build -R plugin
# test_plugin.c — loader, registry, lifecycle
```

## Comparison

| Aspect | Plugin | Buccaneer | Native Door |
|--------|--------|-----------|-------------|
| Language | C | Buccaneer script | Any |
| Process isolation | No (in-process) | No (in-process runtime) | Yes (fork) |
| ABI stability | Published v1.0 | Internal | N/A |
| Hot reload | Replace .so + restart | Recompile | Replace binary |
| Best for | Rich native features | Interpreted addons/games/doors | Legacy DOS/Linux doors |

## Security Notes

- Plugins run as the BBS user with full process privileges
- Only load trusted `.so` files
- Use allowlist in production
- No sandboxing — treat plugins like core BBS code

## Related Documentation

- [Doors and Scripting](doors-and-scripting.md)
- [Configuration](configuration.md) — plugin settings
- [Developer Guide](developer-guide.md) — building from source
