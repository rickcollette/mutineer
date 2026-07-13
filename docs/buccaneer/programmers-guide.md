# Buccaneer Programmer's Guide

A complete guide for writing BBS door programs in Buccaneer. For API details see [Host API Reference](host-api.md); for packaging see [Door Packages](door-packages.md).

## 1. What Buccaneer is

Buccaneer doors run **inside** the BBS process — not as separate shell scripts. Benefits:

- Fast startup per caller
- Per-session VM isolation
- Host-mediated I/O, storage, and messaging
- Capability-based security (`door.json` declares what a door may do)

Buccaneer is **BASIC-like** (familiar keywords, `IF`/`FOR`/`WHILE`) but **structured** (no line numbers, explicit `END SUB`, typed variables).

## 2. Your first program

```basic
PROGRAM "Hello"
VERSION "1.0.0"
CAPABILITY "term.io"
CAPABILITY "user.read"

SUB Main()
    TERM.CLS()
    TERM.PRINTLN("Ahoy from Buccaneer!")
    TERM.PRINTLN("User: " + USER.NAME())
    TERM.PRINTLN("Press a key...")
    DIM k AS STRING
    k = TERM.GETKEY()
END SUB
```

Compile and run locally:

```bash
cd src/buccaneer
make
./bucc examples/hello.bucc -o /tmp/hello.bcm
./bucc-simulator /tmp/hello.bcm
```

## 3. Module structure

Every `.bucc` file is one **module** with metadata headers and declarations.

### Required metadata

| Header | Example | Purpose |
|--------|---------|---------|
| `PROGRAM` | `PROGRAM "My Door"` | Display / package name |
| `VERSION` | `VERSION "1.0.0"` | Semver string |

### Optional metadata

| Header | Purpose |
|--------|---------|
| `AUTHOR` | Author string |
| `DESCRIPTION` | Short description |
| `CAPABILITY` | Requested host powers (repeatable) |
| `DATASET` | Declare logical datasets for `DATA.*` |
| `OPTION` | Compiler/tool options |

### Entry point

Exactly one main procedure:

```basic
SUB Main()
    ' door logic here
END SUB
```

Optional event handlers (when host supports them):

- `OnEnter`, `OnInput`, `OnHangup`, `OnTimeout`, `OnResume`

### Comments

```basic
' This is a comment
REM This is also a comment
```

## 4. Types

| Type | Description |
|------|-------------|
| `INTEGER` | Signed 64-bit integer |
| `DOUBLE` | IEEE-754 floating point |
| `BOOLEAN` | `TRUE` / `FALSE` |
| `STRING` | UTF-8 text |
| `DATE` | Date value |
| `DATETIME` | Date and time |
| `NULL` | Absence of value |
| `ARRAY OF T` | Homogeneous array |
| `MAP OF STRING TO T` | String-keyed map |

### Variables

```basic
DIM score AS INTEGER
DIM name AS STRING
DIM flags AS MAP OF STRING TO BOOLEAN

score = 0
name = "Player1"
```

Parameters and locals use the same `DIM` form inside procedures.

## 5. Procedures and functions

```basic
SUB ShowBanner()
    TERM.PRINTLN("=== Scoreboard ===")
END SUB

FUNCTION AddScore(base AS INTEGER, bonus AS INTEGER) AS INTEGER
    AddScore = base + bonus
END FUNCTION

SUB Main()
    CALL ShowBanner()
    DIM total AS INTEGER
    total = AddScore(10, 5)
    TERM.PRINTLN("Total: " + STR$(total))
END SUB
```

- `SUB` — no return value (use `RETURN` to exit early)
- `FUNCTION` — assign to the function name to return a value

## 6. Operators and expressions

Standard arithmetic: `+`, `-`, `*`, `/`, `\` (integer divide), `MOD`.

Strings concatenate with `+`. Comparisons: `=`, `<>`, `<`, `>`, `<=`, `>=`.

Logical: `AND`, `OR`, `NOT`.

String functions (common): `LEN`, `LEFT$`, `RIGHT$`, `MID$`, `INSTR`, `TRIM$`, `UPPER$`, `LOWER$`, `STR$`, `VAL`.

## 7. Control flow

### IF

```basic
IF score > 100 THEN
    TERM.PRINTLN("High score!")
ELSEIF score > 50 THEN
    TERM.PRINTLN("Good job")
ELSE
    TERM.PRINTLN("Keep trying")
END IF
```

### SELECT CASE

```basic
SELECT CASE choice
    CASE 1
        CALL HandleMessages()
    CASE 2
        CALL HandleFiles()
    CASE ELSE
        TERM.PRINTLN("Unknown option")
END SELECT
```

### FOR

```basic
DIM i AS INTEGER
FOR i = 1 TO 10
    TERM.PRINTLN(STR$(i))
NEXT i
```

### WHILE / DO

```basic
WHILE running
    ' ...
WEND

DO
    ' ...
LOOP UNTIL done
```

## 8. Menu dispatch — ON CALL

Structured alternative to line-number `ON GOTO`:

```basic
DIM choice AS INTEGER
choice = VAL(TERM.INPUT("Selection: ", ""))

ON choice CALL OptMessages, OptFiles, OptQuit
```

Each target must be a `SUB` with a compatible signature. The compiler assigns procedure indices — use the linter to verify dispatch tables.

## 9. Errors — TRY / CATCH

```basic
TRY
    CALL LoadData()
CATCH err
    TERM.PRINTLN("Failed: " + err["message"])
END TRY
```

Classic `ON ERROR GOTO` is **not** supported in v1.

## 10. Exiting and chaining

| Statement | Behavior |
|-----------|----------|
| `DOOR.EXIT()` | Clean exit, code 0 |
| `DOOR.EXIT(1)` | Clean exit with status code |
| `HALT` | Shorthand for `DOOR.EXIT(0)` |
| `RETURN` | Exit current `SUB` only |
| `CHAIN "other_program"` | Transfer to another program in the same door package |
| `CHAIN "other", args` | Chain with argument map |

`CHAIN` never runs an OS executable — only declared Buccaneer programs in the same installed application.

### Application state (APP)

Values survive `CHAIN` within one door app and session:

```basic
APP.SET("area", "general")
DIM area AS STRING
area = APP.GET("area", "default")
```

## 11. Terminal I/O (TERM)

Primary namespace for user interaction. Implemented functions in Mutineer today include:

| Call | Purpose |
|------|---------|
| `TERM.PRINT(s)` | Print without newline |
| `TERM.PRINTLN(s)` | Print line |
| `TERM.CLS()` | Clear screen |
| `TERM.COLOR(fg, bg)` | Set colors |
| `TERM.GETKEY()` | Read one key |
| `TERM.INPUT(prompt, default)` | Read line |
| `TERM.WIDTH()` / `TERM.HEIGHT()` | Terminal size |

Spec-defined but not yet wired in production host: `GOTOXY`, `PAUSE`, `INPUT_PASSWORD`, `SUPPORTS_ANSI`, paging helpers. The **simulator** implements more of the spec — test there first.

See [Host API Reference](host-api.md).

## 12. User and session context

Read-only caller information (requires `user.read` / `session.read` capabilities):

```basic
DIM h AS STRING
h = USER.NAME()
' USER.ALIAS(), USER.ID(), USER.SECURITY(), USER.TIMELEFT()
```

Use `SESSION.GET` / `SESSION.SET` for door-scoped session keys when exposed by the host.

## 13. Door-owned storage

Doors **cannot** write the BBS user/message/file databases directly. Use:

| Namespace | Scope | Use for |
|-----------|-------|---------|
| `KV` | Door-persistent key/value | Settings, small state |
| `DATA` | Structured datasets | Scores, tables, records |
| `APP` | Session + app | Chain-local state |
| `SHARED` | App-wide concurrent | Leaderboards, locks (use carefully) |
| `TEXT` | Mediated files | Logs, data files under door roots |

Example:

```basic
KV.SET("last_played", TERM.TODAY())
DIM v AS STRING
v = KV.GET("last_played", "")
```

`DATA.INSERT`, `DATA.FIND`, `DATA.UPDATE`, etc. operate on logical dataset names declared in metadata.

## 14. Capabilities

Declare every power your door needs:

```basic
CAPABILITY "term.io"
CAPABILITY "user.read"
CAPABILITY "data.read"
CAPABILITY "data.write"
CAPABILITY "kv.write"
```

The host should reject calls without a matching capability (enforcement is being completed — treat declarations as mandatory today).

Common capability strings:

| Capability | Allows |
|------------|--------|
| `term.io` | Terminal input/output |
| `term.query` | Width/height queries |
| `user.read` | `USER.*`, `USERS.FIND` |
| `data.read` / `data.write` | `DATA.*`, some `MSG.*` / `FILE.*` |
| `kv.read` / `kv.write` | `KV.*` |
| `text.read` / `text.write` | `TEXT.*` |
| `bbs.message` | `BBS.SENDMSG` |
| `bbs.query` | `BBS.ONLINE`, `BBS.NODE` |
| `door.chain` | `DOOR.CHAIN` |
| `shared.read` / `shared.write` | `SHARED.*` |

Full matrix: [Door Packages](door-packages.md) and `DOOR_JSON_SCHEMA_SPEC.md`.

## 15. Style and best practices

1. **Compile before deploy** — ship `.bc` modules, not raw `.bucc` on production nodes.
2. **Lint and format** — `bucc-linter` and `bucc-formatter` catch dispatch and type issues early.
3. **Simulator first** — exercise all branches in `bucc-simulator`.
4. **Minimal capabilities** — request only what the door uses.
5. **Short sessions** — respect caller time limits; use `USER.TIMELEFT()` when available.
6. **Handle errors** — wrap file/data operations in `TRY/CATCH`.
7. **No busy loops** — use `SYS.SLEEP(ms)` when polling (sparingly).

## 16. Pascal vs Buccaneer syntax note

Older examples may use `PROCEDURE` / `BEGIN` / `END.` style from early prototypes. Current sources use:

- `SUB` / `END SUB`
- `PROGRAM "name"` string metadata
- `USER.NAME()` function-call syntax

Follow `src/buccaneer/examples/*.bucc` as the canonical style.

## 17. Further reading

- [Host API Reference](host-api.md) — every namespace and function
- [Toolchain](toolchain.md) — compiler and simulator
- [Door Packages](door-packages.md) — `door.json` and install
- `SPECS/BUCCANEER/Buccaneer_SPEC.md` — full platform specification
