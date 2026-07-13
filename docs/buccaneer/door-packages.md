# Buccaneer Door Packages

How to package, declare capabilities, and install multi-program Buccaneer door applications on Mutineer.

## Package layout

```
games/scoreboard/
  door.json              # required manifest
  programs/
    main.bucc
    scores.bucc
  compiled/              # optional: ship .bc here
    main.bc
    scores.bc
  assets/
    banner.txt
```

## door.json (overview)

The manifest tells the BBS how to run the door and what it may access. Full schema: `SPECS/BUCCANEER/DOOR_JSON_SCHEMA_SPEC.md`.

Typical fields:

| Field | Purpose |
|-------|---------|
| `id` | Unique application id |
| `name` | Display name |
| `version` | Package version |
| `entry` | Default program name |
| `programs` | List of compiled modules / source names |
| `capabilities` | Host powers granted at runtime |

Example:

```json
{
  "id": "scoreboard",
  "name": "Scoreboard",
  "version": "1.0.0",
  "entry": "main",
  "programs": ["main", "scores"],
  "capabilities": [
    "term.io",
    "user.read",
    "data.read",
    "data.write",
    "kv.write"
  ]
}
```

Capabilities in `door.json` must be a superset of `CAPABILITY` lines in source metadata.

## Build workflow

1. Write and test `.bucc` in `bucc-simulator`.
2. Run `bucc-linter` on all sources.
3. Compile: `bucc programs/main.bucc -o compiled/main.bc`
4. Validate manifest against schema.
5. Install files under the BBS doors directory and register in the `doors` table.

## Registering on Mutineer

Until `runner=bucc` is wired in `src/doors.c` (see GitHub issue WIRE-3), sysops should:

- Track BUCC integration issues on the repo
- Use the simulator for author acceptance testing
- Plan door records with `runner=bucc` for when launch path lands

Native and DOSBox doors remain the production runners today — see [Doors and Scripting](../doors-and-scripting.md).

## Multi-program applications

Declare multiple programs in `door.json` and chain between them:

```basic
CHAIN "scores"
CHAIN "mail_reader", argsMap
```

`APP` state persists across chains in the same package and caller session.

## Datasets

Declare logical datasets in source or manifest:

```basic
DATASET scores SCHEMA (
    player STRING,
    points INTEGER,
    played_at DATETIME
)
```

Use `DATA.INSERT("scores", row)` in code. Physical SQLite tables are assigned by the host at install time — authors never hard-code table names.

## Security model

1. **Declared capabilities** — `door.json` + `CAPABILITY` headers.
2. **Host enforcement** — dispatch checks capability bitmask (in progress).
3. **No raw SQL** — all persistence via `DATA`, `KV`, `TEXT`.
4. **Throttling** — sensitive calls (`USERS.FIND`, future `MSG.POST`, etc.) rate-limited.

Do not grant `data.write` or `bbs.message` unless the door truly needs them.

## Testing checklist

- [ ] All programs compile without errors
- [ ] Linter passes with zero policy violations
- [ ] Simulator scenarios cover main menu paths
- [ ] Capabilities are minimal
- [ ] `DOOR.EXIT` used instead of falling off end of `Main`
- [ ] CHAIN targets exist in `door.json` `programs` list

## See also

- [Programmer's Guide](programmers-guide.md)
- [Host API Reference](host-api.md)
- [Toolchain](toolchain.md)
