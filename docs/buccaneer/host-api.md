# Buccaneer Host API Reference

Host functions are called from bytecode as `NAMESPACE.NAME(args)`. The Mutineer implementation registers handlers in `src/buccaneer/host.c`. This document reflects **what is registered today**; the [Buccaneer spec](../../SPECS/BUCCANEER/Buccaneer_SPEC.md) may define additional calls not yet wired.

**Legend:** ✅ dispatched · ⚠️ registered but not dispatched · ❌ spec only

## TERM — terminal I/O

| Function | Cap | Args | Returns | Status |
|----------|-----|------|---------|--------|
| `TERM.PRINT(text)` | term.io | 1 | — | ✅ |
| `TERM.PRINTLN(text)` | term.io | 0–1 | — | ✅ |
| `TERM.CLS()` | term.io | 0 | — | ✅ |
| `TERM.COLOR(fg, bg?)` | term.io | 1–2 | — | ✅ |
| `TERM.GOTOXY(x, y)` | term.io | 2 | — | ✅ |
| `TERM.GETKEY()` | term.input | 0 | STRING | ✅ |
| `TERM.INPUT(prompt, default)` | term.input | 0–2 | STRING | ✅ |
| `TERM.WIDTH()` | term.query | 0 | INTEGER | ✅ |
| `TERM.HEIGHT()` | term.query | 0 | INTEGER | ✅ |
| `TERM.PAUSE(ms)` | term.io | 0-1 | — | ✅ |
| `TERM.INPUT_PASSWORD(prompt)` | term.input | 0-1 | STRING | ✅ |
| `TERM.SUPPORTS_ANSI()` | term.query | 0 | BOOLEAN | ✅ |

## USER — current caller

| Function | Cap | Returns |
|----------|-----|---------|
| `USER.NAME()` | user.read | STRING |
| `USER.ALIAS()` | user.read | STRING |
| `USER.ID()` | user.read | INTEGER |
| `USER.SECURITY()` | user.read | INTEGER |
| `USER.TIME_LEFT()` | user.read | INTEGER |
| `USER.TIME_REMAINING()` | user.read | INTEGER |

`USER.TIMELEFT()` remains accepted as a compatibility alias.

`USER.FLAGS()` — spec struct field exists; not yet in dispatch table.

## USERS — directory lookups

| Function | Cap | Notes |
|----------|-----|-------|
| `USERS.FIND(query)` | user.read | Throttled |
| `USERS.GET(handle)` | user.read | |

## SESSION — session-scoped state

| Function | Cap | Notes |
|----------|-----|-------|
| `SESSION.GET(key, default)` | session.read | |
| `SESSION.SET(key, value)` | session.write | |
| `SESSION.NODE()` | session.read | Current node number |
| `SESSION.ELAPSED_MS()` | session.read | Milliseconds since session start |

## BBS — system queries and messaging

| Function | Cap | Notes |
|----------|-----|-------|
| `BBS.SENDMSG(to, text)` | bbs.message | |
| `BBS.ONLINE()` | bbs.query | |
| `BBS.NODE()` | bbs.query | |

## DOOR — control flow

| Function | Cap | Notes |
|----------|-----|-------|
| `DOOR.EXIT(code?)` | — | Exit door and preserve the exit code |
| `DOOR.CHAIN(target, args?)` | door.chain | Request a Buccaneer chain target and args |

## DATA — structured datasets

| Function | Cap | Notes |
|----------|-----|-------|
| `DATA.INSERT(ds, row)` | data.write | |
| `DATA.UPDATE(ds, id, row)` | data.write | |
| `DATA.DELETE(ds, id)` | data.write | |
| `DATA.GET(ds, id)` | data.read | |
| `DATA.FIND(ds, filter…)` | data.read | |
| `DATA.COUNT(ds, filter?)` | data.read | |
| `DATA.BEGIN()` | data.write | Transaction |
| `DATA.COMMIT()` | data.write | |
| `DATA.ROLLBACK()` | data.write | |

## KV — key/value storage

| Function | Cap |
|----------|-----|
| `KV.GET(key, default)` | kv.read |
| `KV.SET(key, value)` | kv.write |
| `KV.DELETE(key)` | kv.write |
| `KV.EXISTS(key)` | kv.read |

## APP — application session state

| Function | Cap |
|----------|-----|
| `APP.GET(key, default)` | — |
| `APP.SET(key, value)` | — |

## SHARED — cross-session app state

| Function | Cap | Notes |
|----------|-----|-------|
| `SHARED.GET(key, default)` | shared.read | Not mutex-protected yet |
| `SHARED.SET(key, value)` | shared.write | |
| `SHARED.CAS(key, expected, new)` | shared.write | Scalar compare-and-swap; structured-value equality is a follow-up |

## TEXT — mediated files

| Function | Cap |
|----------|-----|
| `TEXT.READALL(path)` | text.read |
| `TEXT.READLINES(path)` | text.read |
| `TEXT.WRITEALL(path, content)` | text.write |
| `TEXT.APPEND(path, content)` | text.write |
| `TEXT.EXISTS(path)` | text.read |

Paths use door-managed roots (`data:/`, `temp:/`) as enforced by the host.

## MSG / FILE — read-only BBS surfaces

| Function | Cap |
|----------|-----|
| `MSG.READ(…)` | data.read |
| `MSG.LIST(…)` | data.read |
| `MSG.POST(…)` | data.write |
| `FILE.LIST(…)` | data.read |
| `FILE.INFO(…)` | data.read |

## SYS — utilities

| Function | Args | Returns |
|----------|------|---------|
| `SYS.NOW()` | 0 | DATETIME |
| `SYS.TODAY()` | 0 | DATE |
| `SYS.SLEEP(ms)` | 1 | — |

## Error values

Host calls return error values (catchable via `TRY/CATCH`) when:

- Capability missing (when enforcement enabled)
- API not bound (embedding not configured)
- Invalid arguments
- Throttle exceeded (`USERS.FIND`)

Message shape: map with `"message"` key (see programmer's guide TRY example).

## Embedding note

When running inside Mutineer, API function pointers must be set on `bucc_door_runner_t` with valid context pointers. See open GitHub issues `WIRE-*` for current embedding gaps.

## See also

- [Programmer's Guide](programmers-guide.md)
- [Door Packages](door-packages.md)
