<!-- generated-by: gsd-doc-writer -->

# Files and Protocols

Mutineer BBS manages file areas with upload/download, credit enforcement, batch queues, archive operations, and configurable transfer protocols.

## File Areas

File areas are defined in the `file_areas` table with filesystem paths under `data/` or custom locations.

### Area Fields

| Field | Purpose |
|-------|---------|
| `name` | Display name |
| `path` | Filesystem directory |
| `acs_list` | ACS to list area |
| `acs_download` | ACS to download |
| `acs_upload` | ACS to upload |
| `acs_sysop` | ACS for sysop operations |
| `password` | Optional area password |
| `max_files` | File limit (0=unlimited) |
| `archive_type` | Default archive type |
| `sort_type` | Sort order (0=name, 1=date, 2=size) |
| `free_files` | Files don't cost credits |
| `flags` | Area flags (see below) |

### File Area Flags

| Flag | Constant | Effect |
|------|----------|--------|
| CD-ROM | `FA_FLAG_CDROM` | Read-only area |
| Free files | `FA_FLAG_FREEFILES` | All downloads free |
| No count | `FA_FLAG_NOCOUNT` | Don't count toward ratio |
| No time | `FA_FLAG_NOTIME` | No time deducted |
| Private | `FA_FLAG_PRIVATE` | Private uploads only |
| Slow | `FA_FLAG_SLOW` | Slow media indicator |

## File Records

Stored in `files` table with metadata:

| Field | Purpose |
|-------|---------|
| `filename` | File name in area directory |
| `size_bytes` | File size |
| `description` / `extended_desc` | Short and long descriptions |
| `file_id_diz` | FILE_ID.DIZ content |
| `sha256` | Content hash for duplicate detection |
| `file_points` | Download cost in file points |
| `download_count` | Times downloaded |
| `owner_credit` | Credits to uploader per download |
| `flags` | File flags (validated, offline, free, etc.) |

### File Flags

| Flag | Constant | Effect |
|------|----------|--------|
| Not validated | `FILE_FLAG_NOTVAL` | Awaiting sysop approval |
| Offline | `FILE_FLAG_OFFLINE` | Not available for download |
| Free | `FILE_FLAG_FREE` | No credit charge |
| No time | `FILE_FLAG_NOTIME` | No session time deducted |

## File Commands (F*)

Dispatched by `handle_file_command()` in `src/file_cmds.c`:

| Command | Handler | Description |
|---------|---------|-------------|
| `FA` | `cmd_file_area_change` | Change current file area |
| `FB` | `cmd_file_batch_download` | Download batch queue |
| `FC` | `cmd_file_batch_clear` | Clear batch queue |
| `FD` | `cmd_file_download` | Download file by number |
| `FE` | `cmd_file_extended` | Show extended description |
| `FF` | `cmd_file_find` | Find files by name/description |
| `FG` | `cmd_file_area_list` | List file areas |
| `FJ` | `cmd_file_batch_upload` | Batch upload multiple files |
| `FK` | `cmd_file_batch_remove` | Remove entry from batch queue |
| `FL` | `cmd_file_list` | List files in current area |
| `FN` | `cmd_file_new_scan` | Scan for new files since date |
| `FP` | `cmd_file_set_scan_date` | Set new-scan cutoff date |
| `FQ` | `cmd_file_archive_extract` | Extract archive to temp |
| `FR` | `cmd_file_raw_dir` | Raw directory listing |
| `FT` | `cmd_file_archive_test` | Test archive integrity |
| `FU` | `cmd_file_upload` | Upload file |
| `FV` | `cmd_file_view_archive` | View archive contents |
| `FX` | `cmd_file_expert_toggle` | Toggle expert mode |
| `FZ` | `cmd_file_zippy_search` | Fast wildcard search |

Full reference: [File Commands](reference/file-commands.md).

## Download Flow

1. User lists area (`FG`/`FL`) — ACS check on `acs_list`
2. Select file (`FD`) — ACS check on `acs_download`
3. Credit/ratio enforcement:
   - Cost = `(size_bytes / 1024) + 1` credits unless free
   - Download ratio checked unless `AC_FNODLRATIO` set
   - Daily download limits from security level
4. Protocol launch or raw send
5. Stats updated (`downloads`, `dk`, `dl_today`)

Named art `NOCREDTS` displayed when credits insufficient.

## Upload Flow

1. User enters upload (`FU`) — ACS check on `acs_upload`
2. Protocol receive or path entry
3. Description prompt; FILE_ID.DIZ extracted if present
4. File copied to area path via `file_store_copy()`
5. SHA-256 computed; duplicate check via `db_file_exists_hash()`
6. Record inserted; upload stats incremented
7. Upload queue entry if validation required

## Batch Operations

Session maintains in-memory batch queue (`batch_queue[]`, `batch_count`):

| Command | Action |
|---------|--------|
| Queue file | Select during file browse or `FD` with batch flag |
| `FB` | Download all queued files |
| `FC` | Clear queue |
| `FK` | Remove specific entry |

Menu action `batchrun` triggers batch download from main menu.

## Archive Support

External tools invoked for archive operations:

| Command | Formats |
|---------|---------|
| `FV` | List contents (zip, tar, rar, 7z) |
| `FT` | Integrity test |
| `FQ` | Extract to temp directory |

Archive type tracked in user `def_arc_type` and area `archive_type`.

## Credits and Ratios

### Credits

- Starting credits: config `default_credits` (default 5000)
- Download cost: roughly 1 credit per KB
- Uploads may award credits/file points to owner (`owner_credit`)
- AC flag `AC_FNOCREDITS` bypasses credit checks

### Ratios

Enforced via ACS terms and security level settings:

| Term | Check |
|------|-------|
| `DR` | Upload/download ratio met |
| `Z` | Post ratio met (messages area) |

Security level fields: `download_ratio_num/den`, `ul_dl_ratio_num/den`.

User toggles expert mode with `FX` — affects display detail level (`STATUS_EXPERT`).

## Transfer Protocols

Protocols defined in `protocols` table and optionally `conf/protocols.conf`:

| Field | Purpose |
|-------|---------|
| `name` | Protocol name (ZModem, YModem, etc.) |
| `direction` | `up`, `down`, or `both` |
| `command` | External program argv template |
| `active` | Enabled flag |

`protocol_launch()` executes the configured protocol without a shell. Protocol commands are argv templates: quoted arguments and backslash escapes are supported, `%f` is replaced with the transfer file path as argument text, and the file path is appended as a separate argument when `%f` is omitted. Shell metacharacters such as pipes, redirects, command substitution, `;`, `&&`, and `||` are rejected.

## File Validation

Sysop action `validatefiles` scans file areas for:

- Missing files on disk
- Orphaned disk files not in database
- Invalid descriptions
- Hash mismatches

CLI: `mutineer-validate` for menu/template validation (not file areas).

## Maintenance

| Tool | Purpose |
|------|---------|
| `mutineer-filepack` | Pack/prune file records |
| `mutineer-maint vacuum` | Reclaim DB space after pruning |

In-BBS: `fileadmin` action for area management; `maintenance` for pack operations.

## Upload/Download Queues

Persistent queues for moderated transfers:

| Table | Purpose |
|-------|---------|
| `upload_queue` | Pending uploads awaiting approval |
| `download_queue` | Pending download requests |

## Related Documentation

- [Reference: File Commands](reference/file-commands.md)
- [Configuration](configuration.md) — paths and credits
- [Sysop Guide](sysop-guide.md) — file admin
- [Menus and UI](menus-and-ui.md) — ACS for file areas
