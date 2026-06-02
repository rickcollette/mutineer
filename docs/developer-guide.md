<!-- generated-by: gsd-doc-writer -->

# Developer Guide

Guide for contributors working on Mutineer BBS core, tools, plugins, and tests.

## Build System

CMake 3.16+, C11 standard. Primary build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### CMake Targets

| Target | Output |
|--------|--------|
| `mutineer` | Main BBS server |
| `tools` | All BBS CLI utilities |
| `plank` | PLANK daemons and tools |
| `plugins` | Sample plugins (hello, chat) |

### Compile Flags

- `-Wall -Wextra -Wpedantic` on all targets
- Optional: `-DENABLE_ASAN=ON` for AddressSanitizer

### Dependencies

Required: SQLite3, OpenSSL, pthreads, libdl.

Optional: libargon2, libzstd (PLANK compression).

## Project Structure

```
include/           Public headers (bbs_*.h, plank/*.h)
src/
  main.c           Entry point
  session.c        Largest file — session, menus, handle_action()
  db.c             SQLite layer
  msg_cmds.c       Message commands
  file_cmds.c      File commands
  doors.c          Door launcher
  chat.c           Chat subsystem
  acs.c / mci.c    Access control and templates
  plugin_*.c       Plugin system
  tools/           CLI tool main() files
  plank/           PLANK protocol
  buccaneer/       Scripting language (separate Makefile)
menus/             Menu definitions (operator-editable)
sql/               Database schemas
tests/             Unit and integration tests
plugins/           Sample plugins
conf/              Default configuration
```

## Coding Conventions

- C11 standard, no compiler extensions required
- `snprintf` for all string formatting into buffers
- Error handling: log via `log_*()` and return error codes
- SQLite via `db_*()` wrapper — avoid raw SQL in session code where possible
- ACS checks before privileged operations
- Thread safety: mutex-protect global online list

## Key Entry Points

| Function | File | When Called |
|----------|------|-------------|
| `main()` | `main.c` | Process start |
| `net_run_listener()` | `net_listener.c` | Accept loop |
| `session_run()` | `session.c` | Per-connection thread |
| `handle_action()` | `session.c` | Menu selection |
| `handle_msg_command()` | `msg_cmds.c` | M*/R* input |
| `handle_file_command()` | `file_cmds.c` | F* input |
| `cfg_load()` | `config.c` | Config parse |
| `db_open()` | `db.c` | Database open |

## Adding a Menu Action

1. Choose action name (lowercase, no spaces)
2. Add handler branch in `handle_action()` in `session.c`
3. Add menu item in appropriate `.mnu` file
4. Document in `docs/reference/menu-actions.md`
5. Add expect test if interactive behavior

For command-style actions (M*/F*), add to `msg_cmds.c` or `file_cmds.c` instead.

## Adding a CLI Tool

1. Create `src/tools/mutineer-mytool.c`
2. Add to CMakeLists.txt via `add_mutineer_tool(mutineer-mytool)`
3. Share common sources via `TOOL_COMMON_SOURCES`
4. Document in `docs/cli-tools.md`
5. Add test in `tests/test_tools.c` or shell script

## Database Changes

1. Edit `sql/schema.sql` (or `sql/plank_schema.sql` for PLANK)
2. Add migration logic in `startup.c` or `db.c` if needed
3. Update `docs/reference/database.md`
4. Update `mutineer-initbbs` seed data if applicable

## Testing

### Unit Tests (ctest)

```bash
cmake --build build
ctest --test-dir build --exclude-regex "tools_cli|expect_suite"
```

| Test | Covers |
|------|--------|
| `test_acs` | ACS expression parser |
| `test_mci` | MCI token expansion |
| `test_menu` | Menu file parsing |
| `test_menu_template` | Template rendering |
| `test_doors` | Door launcher |
| `test_plugin` | Plugin loader |
| `test_plank_*` | PLANK subsystems |
| `test_tools` | CLI tool DB operations |
| `test_file_cmds` | File command logic |
| `test_smoke` | Basic integration |

### CLI Tests

```bash
ctest --test-dir build -R tools_cli
# runs tests/test_tools_cli.sh
```

### Interactive Tests

Requires `expect`:

```bash
ctest --test-dir build -R expect_suite
# runs tests/run_expect_tests.sh
```

Tests: login, auth, messages, files, chat, admin, vote, multilogin, etc.

### Menu Validation

```bash
build/mutineer-validate menus
```

Always run before committing menu changes.

## Debug Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
```

Run under debugger:

```bash
gdb --args build/mutineer --config conf/mutineer.conf
```

## Plugin Development

See [Plugins](plugins.md). Build with:

```bash
cmake --build build --target plugins
```

Test:

```bash
ctest --test-dir build -R plugin
```

## Buccaneer Development

Separate Makefile in `src/buccaneer/`:

```bash
cd src/buccaneer && make test
```

## Branch Conventions

No formal convention documented. Use descriptive branch names:

- `feat/my-feature`
- `fix/bug-description`
- `docs/update-guide`

## Pull Request Guidelines

Before submitting:

1. Build succeeds with `-Wall -Wextra -Wpedantic`
2. `ctest --test-dir build --exclude-regex "tools_cli|expect_suite"` passes
3. `mutineer-validate menus` passes if menus changed
4. Document new config keys in `docs/configuration.md`
5. Document new actions/commands in reference docs

## License

Mutineer BBS is MIT licensed. See [LICENSE](../LICENSE).

Contributions are accepted under the same license.

## Related Documentation

- [Architecture](architecture.md)
- [CLI Tools](cli-tools.md)
- [Getting Started](getting-started.md)
- [Reference](reference/)
