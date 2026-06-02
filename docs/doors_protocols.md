# Doors & Protocols

## Doors
- Define in SQLite `doors` table (schema in `sql/schema.sql`):
  - `name`, `dropfile` (e.g., `DOOR.SYS`), `command`, optional `workdir`, `acs`.
- Mutineer writes dropfiles to `dropfile_path/<name>/`.
- Minimal `DOOR.SYS` and `DORINFO1.DEF` are emitted; customize door command to consume them.
- Action `doors` lists available entries; ACS enforced per door.

## Protocols
- Configure in `protocols` table: `name`, `direction` (`up`/`down`/`both`), `command`, `active`.
- File downloads pick the first active `down/both` protocol; uploads can be wired similarly.
- Example entry: `Zmodem`, `down`, `sz %f`, `active=1`.

## Sandbox
- Doors run with `workdir` if provided; ensure binaries are in PATH or use absolute paths.
- Dropfile directory is isolated per door to reduce cross-talk.
